#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "stb_ds.h"
#include "common.h"
#include "gpudl.h"

#include "embedded_resources.h"

#define MAX_WINDOWS (16)

#define VTXBUF_SZ (1<<22) // 4MB

#define STATIC_QUADS_IDXBUF_SZ ((1<<16)+(1<<15))
// The static quad index buffer contains indices like this:
//  [
//   0,1,2,0,2,3,
//   4,5,6,4,6,7,
//   ...
//   65532,65533,65534,65532,65534,65535
//  ]
// It's for drawing quads from triangles like this:
//   0---1
//   |\  |
//   | \ |   (add quad_index*4)
//   |  \|
//   3---2
// I.e. it allows me to add 4 vertices to the vertex buffer in order to draw a
// quad, and the index buffer expands each quad to 2 triangles. Since there are
// 6 indices per 4 vertices, the size must be 65536*(6/4) (to "max it out")

enum {
	R_MODE_ATLAS = 1,
	R_MODE_PLOT,
	R_MODE_VECTOR,
};

#if 0
enum postproc_type {
	PP_NONE = 1,
	PP_GAUSS,
	//PP_XGAUSS,
	//PP_YGAUSS,
	//PP_NOISY,
};

struct postproc {
	enum postproc_type previous_type;
	enum postproc_type type;
};

struct postproc_framebuf {
	WGPUTexture texture;
	WGPUTextureView texture_view;
};

#define MAX_POSTPROC_FRAMEBUFS (2)

struct postproc_framebufs {
	struct postproc_framebuf bufs[MAX_POSTPROC_FRAMEBUFS];
};
#endif

struct window {
	int id;
	int width;
	int height;
	#if 0
	struct postproc_framebufs postproc_framebufs;
	#endif
};

struct atlas_vtx {
	union v2 a_pos;
	union v2 a_uv;
	uint32_t a_color;
};

struct atlas_uni {
	int dst_dim[2];
	// TODO
};

struct plot_vtx {
	union v2 a_pos;
	union v2 a_uv;
	float a_threshold;
	uint32_t a_color;
};

struct plot_uni {
	int dst_dim[2];
	// TODO
};

struct vector_vtx {
	union v2 a_pos;
	uint32_t a_color;
};

struct vector_uni {
	int dst_dim[2];
};

struct r {
	int mode;
	struct window* window;
	WGPUTextureView render_target_texture_view;
	int n_passes;
	int cursor0;
	int n_static_quad_indices;
	WGPUCommandEncoder encoder;
	WGPURenderPipeline pipeline;
	WGPUBindGroup bind_group;
};

struct mprg {
	int n_windows;
	struct window windows[MAX_WINDOWS];

	WGPUDevice device;
	WGPUQueue queue;

	WGPUBuffer static_quad_idxbuf;
	WGPUBuffer vtxbuf;

	int vtxbuf_cursor;
	uint8_t vtxbuf_data[VTXBUF_SZ];

	struct r r;

	WGPUBuffer          vector_unibuf;
	WGPURenderPipeline  vector_pipeline;
	WGPUBindGroup       vector_bind_group;
} mprg;

static void new_window()
{
	if (mprg.n_windows >= MAX_WINDOWS) return;
	struct window* w = &mprg.windows[mprg.n_windows++];
	w->id = gpudl_window_open("musikprogram");
}

static void window_update_size(struct window* window)
{
	int prev_width = window->width;
	int prev_height = window->height;
	gpudl_window_get_size(window->id, &window->width, &window->height);

	if (window->width != prev_width || window->height != prev_height) {
		printf("TODO update per-window post processing stuff?\n");
	}
}

static WGPUShaderModule mk_shader_module(WGPUDevice device, const char* src)
{
	WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &(WGPUShaderModuleDescriptor) {
		.nextInChain = (const WGPUChainedStruct*) &(WGPUShaderModuleWGSLDescriptor) {
			.chain = { .sType = WGPUSType_ShaderModuleWGSLDescriptor },
			.code = src,
		},
	});
	assert(shader);
	return shader;
}

static void r_flush(int do_submit_queue)
{
	struct r* r = &mprg.r;
	assert(mprg.vtxbuf_cursor >= r->cursor0);
	const int n = (mprg.vtxbuf_cursor - r->cursor0);
	if (n == 0 && !do_submit_queue) return; // nothing to do

	if (r->encoder == NULL) {
		if (do_submit_queue && n == 0) {
			// don't submit empty queue
			return;
		}
		r->encoder = wgpuDeviceCreateCommandEncoder(mprg.device, &(WGPUCommandEncoderDescriptor){});
		assert(r->encoder != NULL);
	}

	if (n > 0) {
		assert(r->mode > 0);

		assert(r->encoder != NULL);
		assert(r->render_target_texture_view != NULL);
		WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
			r->encoder,
			&(WGPURenderPassDescriptor){
				.colorAttachmentCount = 1,
				.colorAttachments = &(WGPURenderPassColorAttachment){
					.view = r->render_target_texture_view,
					.resolveTarget = 0,
					.loadOp = r->n_passes == 0 ? WGPULoadOp_Clear : WGPULoadOp_Load, // first pass clears the target
					.storeOp = WGPUStoreOp_Store,
					.clearValue = (WGPUColor){},
				},
				.depthStencilAttachment = NULL,
			}
		);

		assert(r->pipeline != NULL);
		wgpuRenderPassEncoderSetPipeline(pass, r->pipeline);

		if (r->bind_group != NULL) {
			wgpuRenderPassEncoderSetBindGroup(pass, 0, r->bind_group, 0, 0);
		}

		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mprg.vtxbuf, r->cursor0, n);

		wgpuRenderPassEncoderSetIndexBuffer(
			pass,
			mprg.static_quad_idxbuf,
			WGPUIndexFormat_Uint16,
			0,
			sizeof(uint16_t)*r->n_static_quad_indices);
		wgpuRenderPassEncoderDrawIndexed(pass, r->n_static_quad_indices, 1, 0, 0, 0);

		wgpuRenderPassEncoderEnd(pass);
		r->n_passes++;
		r->cursor0 = mprg.vtxbuf_cursor;
		r->n_static_quad_indices = 0;
	}

	if (do_submit_queue) {
		assert(r->encoder != NULL);

		// XXX assuming that wgpuRenderPassEncoderSetVertexBuffer()
		// doesn't do any buffer copying, so that it's fine to write
		// the entire vertex buffer just before submitting the queue.
		// if this is not the case, do wgpuQueueWriteBuffer() with
		// appropriate offset before setting the vertex buffer?
		wgpuQueueWriteBuffer(mprg.queue, mprg.vtxbuf, 0, &mprg.vtxbuf_data, mprg.vtxbuf_cursor);

		WGPUCommandBuffer cmdbuf = wgpuCommandEncoderFinish(r->encoder, &(WGPUCommandBufferDescriptor){});
		wgpuQueueSubmit(mprg.queue, 1, &cmdbuf);
		r->encoder = NULL;
		mprg.vtxbuf_cursor = 0;
	}
}

static void r_begin_frame(struct window* w, WGPUTextureView swap_chain_texture_view)
{
	struct r* r = &mprg.r;
	assert((r->window == NULL) && "frame already begun");
	assert(mprg.vtxbuf_cursor == 0);

	memset(r, 0, sizeof *r);
	r->window = w;
	r->render_target_texture_view = swap_chain_texture_view; // XXX this is not always the case...

	// write all uniform buffers;
	//  - optimistically assuming that all uniform buffers are probably
	//    going to be used...
	//  - if some buffers are NOT used, I'm assuming writes are cheap :-)
	//  - contents are invariant ("uniform") for an entire frame, so this
	//    is a good place to write uniforms; at the beginning of a "frame"
	//  - since uniforms belong to a bind group it doesn't make a lot of
	//    sense to do "intra-frame uniform buffer writes" anyway?

	{
		struct vector_uni u = {
			.dst_dim = { w->width, w->height },
		};
		wgpuQueueWriteBuffer(mprg.queue, mprg.vector_unibuf, 0, &u, sizeof u);
	}
}

static void r_end_frame()
{
	struct r* r = &mprg.r;
	assert((r->mode == 0) && "mode not r_end()'d");
	assert((r->window != NULL) && "r_begin_frame() not called");
	r_flush(1);
	r->window = NULL;
}

static void r_begin(int mode)
{
	struct r* r = &mprg.r;
	assert((r->mode == 0) && "already in a mode");

	switch (mode) {
	case R_MODE_VECTOR:
		r->pipeline = mprg.vector_pipeline;
		r->bind_group = mprg.vector_bind_group;
		break;
	default: assert(!"unhandled mode");
	}

	r->cursor0 = mprg.vtxbuf_cursor;
	r->mode = mode;
}

static void r_end()
{
	r_flush(0);
	struct r* r = &mprg.r;
	r->mode = 0;
}

static void* r_request(size_t vtxbuf_requested, int idxbuf_requested)
{
	assert(((vtxbuf_requested&3) == 0) && "vtxbuf_requested must be 4-byte aligned");
	struct r* r = &mprg.r;
	int vtxbuf_required = mprg.vtxbuf_cursor + vtxbuf_requested;
	int idxbuf_required = r->n_static_quad_indices + idxbuf_requested;
	if ((vtxbuf_required > VTXBUF_SZ) || (idxbuf_required > STATIC_QUADS_IDXBUF_SZ)) r_flush(1);
	assert((mprg.vtxbuf_cursor + vtxbuf_requested) <= VTXBUF_SZ);
	assert((r->n_static_quad_indices + idxbuf_requested ) <= STATIC_QUADS_IDXBUF_SZ);

	void* p = mprg.vtxbuf_data + mprg.vtxbuf_cursor;
	mprg.vtxbuf_cursor += vtxbuf_requested;
	assert((mprg.vtxbuf_cursor&3) == 0);

	r->n_static_quad_indices += idxbuf_requested;

	return p;
}

static void rv_quad(float x, float y, float w, float h, uint32_t color)
{
	struct vector_vtx* pv = r_request(4 * sizeof(*pv), 6);

	pv[0].a_pos.x = x;
	pv[0].a_pos.y = y;
	pv[0].a_color = color;

	pv[1].a_pos.x = x+w;
	pv[1].a_pos.y = y;
	pv[1].a_color = color;

	pv[2].a_pos.x = x+w;
	pv[2].a_pos.y = y+h;
	pv[2].a_color = color;

	pv[3].a_pos.x = x;
	pv[3].a_pos.y = y+h;
	pv[3].a_color = color;
}

int main(int argc, char** argv)
{
	gpudl_init();

	new_window();

	WGPUAdapter adapter;
	WGPUDevice device;
	WGPUQueue queue;
	gpudl_get_wgpu(NULL, &adapter, &device, &queue);
	mprg.device = device;
	mprg.queue = queue;

	{
		// prepare "static quads index buffer"; see
		// STATIC_QUADS_IDXBUF_SZ comment
		uint16_t indices[STATIC_QUADS_IDXBUF_SZ];
		size_t size = sizeof(indices[0])*STATIC_QUADS_IDXBUF_SZ;
		mprg.static_quad_idxbuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
			.size = size,
		});
		assert(mprg.static_quad_idxbuf);
		int offset = 0;
		for (int i = 0; i < STATIC_QUADS_IDXBUF_SZ; i += 6, offset += 4) {
			assert(0 <= offset && offset <= (65536-4));
			indices[i+0] = offset+0;
			indices[i+1] = offset+1;
			indices[i+2] = offset+2;
			indices[i+3] = offset+0;
			indices[i+4] = offset+2;
			indices[i+5] = offset+3;
		}
		wgpuQueueWriteBuffer(queue, mprg.static_quad_idxbuf, 0, indices, size);
	}

	// prepare dynamic vertex buffer
	mprg.vtxbuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
		.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
		.size = sizeof(mprg.vtxbuf_data),
	});
	assert(mprg.vtxbuf);
	//printf("vtxbuf is %zd bytes\n", sizeof(mprg.vtxbuf_data));

	// R_MODE_VECTOR
	{
		const size_t unisz = sizeof(struct vector_uni);

		mprg.vector_unibuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
			.size = unisz,
		});
		assert(mprg.vector_unibuf);

		WGPUShaderModule shader = mk_shader_module(device, shadersrc_vector);


		WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(
			device,
			&(WGPUBindGroupLayoutDescriptor){
				.entryCount = 1,
				.entries = (WGPUBindGroupLayoutEntry[]){
					(WGPUBindGroupLayoutEntry){
						.binding = 0,
						.visibility = WGPUShaderStage_Vertex,
						.buffer = (WGPUBufferBindingLayout){
							.type = WGPUBufferBindingType_Uniform,
							.hasDynamicOffset = false,
							.minBindingSize = unisz,
						},
					},
				},
			}
		);
		assert(bind_group_layout);

		mprg.vector_bind_group = wgpuDeviceCreateBindGroup(device, &(WGPUBindGroupDescriptor){
			.layout = bind_group_layout,
			.entryCount = 1,
			.entries = (WGPUBindGroupEntry[]){
				(WGPUBindGroupEntry){
					.binding = 0,
					.buffer = mprg.vector_unibuf,
					.offset = 0,
					.size = unisz,
				},
			},
		});

		WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
			device,
			&(WGPUPipelineLayoutDescriptor){
				.bindGroupLayoutCount = 1,
				.bindGroupLayouts = (WGPUBindGroupLayout[]){
					bind_group_layout,
				},
			}
		);
		assert(pipeline_layout);

		mprg.vector_pipeline = wgpuDeviceCreateRenderPipeline(
			device,
			&(WGPURenderPipelineDescriptor){
				.layout = pipeline_layout,
				.vertex = (WGPUVertexState){
					.module = shader,
					.entryPoint = "vs_main",
					.bufferCount = 1,
					.buffers = (WGPUVertexBufferLayout[]){
						(WGPUVertexBufferLayout){
							.arrayStride = sizeof(struct vector_vtx),
							.stepMode = WGPUVertexStepMode_Vertex,
							.attributeCount = 2,
							.attributes = (WGPUVertexAttribute[]) {
								(WGPUVertexAttribute){
									.format = WGPUVertexFormat_Float32x2,
									.offset = (uint64_t)MEMBER_OFFSET(struct vector_vtx, a_pos),
									.shaderLocation = 0,
								},
								(WGPUVertexAttribute){
									.format = WGPUVertexFormat_Unorm8x4,
									.offset = (uint64_t)MEMBER_OFFSET(struct vector_vtx, a_color),
									.shaderLocation = 1,
								},
							},
						},
					},
				},
				.primitive = (WGPUPrimitiveState){
					.topology = WGPUPrimitiveTopology_TriangleList,
					.frontFace = WGPUFrontFace_CCW,
					.cullMode = WGPUCullMode_None
				},
				.multisample = (WGPUMultisampleState){
					.count = 1,
					.mask = ~0,
					.alphaToCoverageEnabled = false,
				},
				.fragment = &(WGPUFragmentState){
					.module = shader,
					.entryPoint = "fs_main",
					.targetCount = 1,
					.targets = &(WGPUColorTargetState){
						.format = gpudl_get_preferred_swap_chain_texture_format(),
						.blend = &(WGPUBlendState){
							.color = (WGPUBlendComponent){
								.srcFactor = WGPUBlendFactor_One,
								.dstFactor = WGPUBlendFactor_Zero,
								.operation = WGPUBlendOperation_Add,
							},
							.alpha = (WGPUBlendComponent){
								.srcFactor = WGPUBlendFactor_One,
								.dstFactor = WGPUBlendFactor_Zero,
								.operation = WGPUBlendOperation_Add,
							}
						},
						.writeMask = WGPUColorWriteMask_All
					},
				},
				.depthStencil = NULL,
			}
		);
	}

	while (mprg.n_windows > 0) {
		struct gpudl_event e;
		while (gpudl_poll_event(&e)) {
			struct window* w = NULL;
			int widx = -1;

			// find event window
			for (int i = 0; i < mprg.n_windows; i++) {
				struct window* ww = &mprg.windows[i];
				if (ww->id == e.window_id) {
					w = ww;
					widx = i;
					break;
				}
			}
			int do_close = 0;

			switch (e.type) {
			case GPUDL_CLOSE:
				do_close = 1;
				break;
			case GPUDL_KEY:
				if (e.key.code == '\033') do_close = 1;
				break;
			default:
				break;
			}

			if (do_close && widx >= 0) {
				assert(0 <= widx && widx < mprg.n_windows);
				gpudl_window_close(w->id);
				int n_move = (mprg.n_windows - widx) - 1;
				if (n_move > 0) {
					memmove(&mprg.windows[widx], &mprg.windows[widx+1], n_move * sizeof(mprg.windows[0]));
				}
				mprg.n_windows--;
			}
		}

		// render all windows
		for (int i = 0; i < mprg.n_windows; i++) {
			struct window* window = &mprg.windows[i];
			WGPUTextureView v = gpudl_render_begin(window->id);
			if (!v) continue;

			window_update_size(window);

			r_begin_frame(window, v);

			r_begin(R_MODE_VECTOR);
			rv_quad(0,               0,                window->width/2, window->height/2, 0xff0000ff);
			rv_quad(window->width/2, 0,                window->width/2, window->height/2, 0xff00ff00);
			r_end();

			r_begin(R_MODE_VECTOR);
			rv_quad(0,               window->height/2, window->width/2, window->height/2, 0xffff0000);
			rv_quad(window->width/2, window->height/2, window->width/2, window->height/2, 0xff00ffff);
			r_end();

			r_end_frame();

			gpudl_render_end();
		}
	}

	return EXIT_SUCCESS;
}
