#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "stb_ds.h"
#include "common.h"
#include "gpudl.h"

#include "embedded_resources.h"

#define MAX_WINDOWS (16)

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

// Max Buffer Elements (element=quad)
#define MBE_ATLAS  (1<<14)
#define MBE_PLOT   (1<<6)
#define MBE_VECTOR (1<<14)
#define MBE_ALL    MAX(MAX(MBE_ATLAS,MBE_PLOT),MBE_VECTOR)

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

#define UNIBUF_SZ MAX(MAX(sizeof(struct atlas_uni), sizeof(struct plot_uni)), sizeof(struct vector_uni))

struct r {
	int mode;
	struct window* window;
	WGPUTextureView swap_chain_texture_view;
	int pass;
};

struct mprg {
	int n_windows;
	struct window windows[MAX_WINDOWS];

	WGPUDevice device;
	WGPUQueue queue;

	WGPUBuffer idxbuf;
	WGPUBuffer vtxbuf;
	WGPUBuffer unibuf;

	int vtxbuf_cursor;
	union {
		struct atlas_vtx   atlas[4*MBE_ATLAS];
		struct plot_vtx    plot[4*MBE_PLOT];
		struct vector_vtx  vector[4*MBE_VECTOR];
	} vtxbuf_data;

	struct r r;

	WGPURenderPipeline pipeline_vector;
	WGPUBindGroup bind_group_vector;
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

static void r_begin_frame(struct window* w, WGPUTextureView v)
{
	struct r* r = &mprg.r;
	assert((r->window == NULL) && "frame already begun");
	r->window = w;
	r->swap_chain_texture_view = v;
	r->pass = 0;
}

static void r_end_frame()
{
	struct r* r = &mprg.r;
	assert((r->mode == 0) && "mode not r_end()'d");
	assert((r->window != NULL) && "r_begin_frame() not called");
	r->window = NULL;
}

static void r_begin(int mode)
{
	struct r* r = &mprg.r;
	assert((r->mode == 0) && "already in a mode");

	switch (mode) {
	case R_MODE_VECTOR:
		break;
	default: assert(!"unhandled mode");
	}

	r->mode = mode;
}

static void r_end()
{
	struct r* r = &mprg.r;
	assert((r->mode > 0) && "not in a mode");
	const int mode = r->mode;
	r->mode = 0;
	const int n_elements = mprg.vtxbuf_cursor;
	mprg.vtxbuf_cursor = 0;

	if (n_elements <= 0) return;

	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(mprg.device, &(WGPUCommandEncoderDescriptor){});

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
		encoder,
		&(WGPURenderPassDescriptor){
			.colorAttachmentCount = 1,
			.colorAttachments = &(WGPURenderPassColorAttachment){
			.view = r->swap_chain_texture_view, // XXX ask postproc stuff; we might not be rendering to swap chain texture
			.resolveTarget = 0,
			.loadOp = r->pass == 0 ? WGPULoadOp_Clear : WGPULoadOp_Load,
			.clearValue = (WGPUColor){},
			.storeOp = WGPUStoreOp_Store,
		},
		.depthStencilAttachment = NULL,
	});


	WGPURenderPipeline pipeline = NULL;
	WGPUBindGroup bind_group = NULL;
	size_t vtxsz = 0;
	size_t unisz = 0;

	union {
		struct atlas_uni atlas;
		struct plot_uni plot;
		struct vector_uni vector;
	} unidata;

	switch (mode) {
	case R_MODE_VECTOR:
		pipeline = mprg.pipeline_vector;
		bind_group = mprg.bind_group_vector;
		vtxsz = sizeof(mprg.vtxbuf_data.vector[0]);
		unisz = sizeof(unidata.vector);
		struct vector_uni* u = &unidata.vector;
		u->dst_dim[0] = r->window->width;
		u->dst_dim[1] = r->window->height;
		break;
	default: assert(!"unhandled mode");
	}

	assert(pipeline != NULL);
	assert(bind_group != NULL);
	assert(vtxsz > 0);
	assert(unisz > 0);

	const int n_indices  = 6*n_elements;
	const int n_vertices = 4*n_elements;

	wgpuQueueWriteBuffer(mprg.queue, mprg.vtxbuf, 0, &mprg.vtxbuf_data, n_vertices*vtxsz);
	wgpuQueueWriteBuffer(mprg.queue, mprg.unibuf, 0, &unidata, unisz);

	wgpuRenderPassEncoderSetPipeline(pass, pipeline);
	wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, 0);
	wgpuRenderPassEncoderSetIndexBuffer(pass, mprg.idxbuf, WGPUIndexFormat_Uint16, 0, sizeof(uint16_t)*n_indices);
	wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mprg.vtxbuf, 0, vtxsz*n_vertices);
	wgpuRenderPassEncoderDrawIndexed(pass, n_indices, 1, 0, 0, 0);

	wgpuRenderPassEncoderEnd(pass);

	WGPUCommandBuffer cmdbuf = wgpuCommandEncoderFinish(encoder, &(WGPUCommandBufferDescriptor){});
	wgpuQueueSubmit(mprg.queue, 1, &cmdbuf);

	r->pass++;
}

static void rv_quad(float x, float y, float w, float h, uint32_t color)
{
	// XXX no overflow handling...
	struct vector_vtx* pv = &mprg.vtxbuf_data.vector[4*mprg.vtxbuf_cursor++];

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
		// we're drawing a lot of quads via triangles:
		//   0---1
		//   |\  |
		//   | \ |
		//   |  \|
		//   3---2
		// so prepare static index buffer with indices like:
		//   0,1,2,0,2,3,
		//   4,5,6,4,6,7,
		//   and so on...
		const int n_indices = 6*MBE_ALL; // 4 vertices per quad; 2 triangles; 6 indices
		uint16_t indices[n_indices];
		size_t size = sizeof(indices[0])*n_indices;
		mprg.idxbuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
			.size = size,
		});
		assert(mprg.idxbuf);
		int offset = 0;
		for (int i = 0; i < n_indices; i += 6, offset += 4) {
			indices[i+0] = offset+0;
			indices[i+1] = offset+1;
			indices[i+2] = offset+2;
			indices[i+3] = offset+0;
			indices[i+4] = offset+2;
			indices[i+5] = offset+3;
		}
		wgpuQueueWriteBuffer(queue, mprg.idxbuf, 0, indices, size);
	}

	// prepare dynamic vertex buffer
	mprg.vtxbuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
		.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
		.size = sizeof(mprg.vtxbuf_data),
	});
	assert(mprg.vtxbuf);
	//printf("vtxbuf is %zd bytes\n", sizeof(mprg.vtxbuf_data));

	// prepare uniform buffer
	mprg.unibuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
		.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
		.size = UNIBUF_SZ,
	});
	assert(mprg.unibuf);


	// R_MODE_VECTOR
	{
		WGPUShaderModule shader = mk_shader_module(device, shadersrc_vector);

		const size_t unisz = sizeof(struct vector_uni);

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

		mprg.bind_group_vector = wgpuDeviceCreateBindGroup(device, &(WGPUBindGroupDescriptor){
			.layout = bind_group_layout,
			.entryCount = 1,
			.entries = (WGPUBindGroupEntry[]){
				(WGPUBindGroupEntry){
					.binding = 0,
					.buffer = mprg.unibuf,
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

		mprg.pipeline_vector = wgpuDeviceCreateRenderPipeline(
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
			rv_quad(0, 0, 100, 100, 0xffffffff);
			rv_quad(500, 200, 100, 300, 0xffffffff);
			rv_quad(200, 200, 100, 300, 0xffffffff);
			r_end();

			r_end_frame();

			gpudl_render_end();
		}
	}

	return EXIT_SUCCESS;
}
