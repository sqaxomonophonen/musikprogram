#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "stb_ds.h"
#include "sokol_time.h"
#include "common.h"
#include "gpudl.h"
#include "fps.h"

#include "embedded_resources.h"

#define MAX_WINDOWS (16)

#define FRAMEBUFFER_DEFAULT_TEXTURE_FORMAT (WGPUTextureFormat_BGRA8UnormSrgb)

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

struct ppgauss_uni {
	float width;
	float height;
	float seed;
	float sigma;
	float rstep;
	float broken;
	int n0;
	int n1;
};

enum postproc_type {
	PP_NONE,
	PP_GAUSS,
	//PP_XGAUSS,
	//PP_YGAUSS,
	//PP_NOISY,
};

struct postproc {
	enum postproc_type previous_type;
	enum postproc_type type;

	float scalar;

	WGPUBuffer           gauss_unibuf;
	WGPUBindGroupLayout  gauss_bind_group_layout;
	WGPURenderPipeline   gauss_pipeline;
	WGPUSampler          gauss_sampler;
};

struct postproc_framebuf {
	WGPUTexture      texture;
	WGPUTextureView  texture_view;
	WGPUBindGroup    bind_group;
};

#define MAX_POSTPROC_FRAMEBUFS (1)

struct postproc_window {
	int width;
	int height;
	WGPUTextureView swap_chain_texture_view;
	struct postproc_framebuf fb[MAX_POSTPROC_FRAMEBUFS];
};

struct window {
	int id;
	int width;
	int height;
	struct postproc_window ppw;
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
	uint16_t a_color[4];
};

struct vector_uni {
	int dst_dim[2];
	float seed;
	float scalar;
};

struct r {
	int begun_frames;
	int begun_frame;
	int mode;
	struct window* window;
	WGPUTextureView render_target_texture_view;
	int n_passes;
	int cursor0;
	int n_static_quad_indices;
	WGPUCommandEncoder encoder;
	WGPURenderPipeline pipeline;
	WGPUBindGroup bind_group;
	float seed;
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

	struct postproc postproc;
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

static WGPUTextureView postproc_begin_frame(struct window* window, WGPUTextureView swap_chain_texture_view)
{
	struct postproc* pp = &mprg.postproc;
	struct postproc_window* ppw = &window->ppw;
	ppw->swap_chain_texture_view = swap_chain_texture_view;

	const int type_has_changed = pp->type != pp->previous_type;
	const int dimensions_have_changed = (ppw->width != window->width) || (ppw->height != window->height);

	const int must_reinitialize = type_has_changed || dimensions_have_changed;

	if (must_reinitialize) {
		ppw->width = window->width;
		ppw->height = window->height;

		// cleanup
		switch (pp->previous_type) {
		case PP_NONE: break;
		case PP_GAUSS: {
			wgpuBindGroupDrop(ppw->fb[0].bind_group);
			wgpuTextureViewDrop(ppw->fb[0].texture_view);
			wgpuTextureDrop(ppw->fb[0].texture);
			} break;
		}

		// create new per-window resources
		switch (pp->type) {
		case PP_NONE: break;
		case PP_GAUSS: {
			 WGPUTexture texture = wgpuDeviceCreateTexture(
			 	mprg.device,
				&(WGPUTextureDescriptor) {
					.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
					.dimension = WGPUTextureDimension_2D,
					.size = (WGPUExtent3D){
						.width = ppw->width,
						.height = ppw->height,
						.depthOrArrayLayers = 1,
					},
					.mipLevelCount = 1,
					.sampleCount = 1,
					.format = FRAMEBUFFER_DEFAULT_TEXTURE_FORMAT,
				}
			);
			assert(texture);
			ppw->fb[0].texture = texture;

			WGPUTextureView view = wgpuTextureCreateView(texture, &(WGPUTextureViewDescriptor) {});
			assert(view);
			ppw->fb[0].texture_view = view;

			WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
				mprg.device,
				&(WGPUBindGroupDescriptor){
					.layout = pp->gauss_bind_group_layout,
					.entryCount = 3,
					.entries = (WGPUBindGroupEntry[]){
						(WGPUBindGroupEntry){
							.binding = 0,
							.buffer = pp->gauss_unibuf,
							.offset = 0,
							.size = sizeof(struct ppgauss_uni),
						},
						(WGPUBindGroupEntry){
							.binding = 1,
							.textureView = view,
						},
						(WGPUBindGroupEntry){
							.binding = 2,
							.sampler = pp->gauss_sampler,
						},
					},
				}
			);
			assert(bind_group);
			ppw->fb[0].bind_group = bind_group;

			} break;
		}

	}

	switch (pp->type) {
	case PP_NONE: return swap_chain_texture_view;
	case PP_GAUSS: return ppw->fb[0].texture_view;
	default: assert(!"unhandled postproc type");
	}
}

static void postproc_end_frame(WGPUCommandEncoder encoder)
{
	struct r* r = &mprg.r;
	struct postproc* pp = &mprg.postproc;
	struct postproc_window* ppw = &r->window->ppw;

	switch (pp->type) {
	case PP_NONE: break;
	case PP_GAUSS: {
		struct ppgauss_uni u = {
			//.dst_dim = {ppw->width, ppw->height},
			.width = ppw->width,
			.height = ppw->height,
			.seed = r->seed,
			.sigma = 0.003f,
			.rstep = 14.31f,
			.broken = 0.0f,
			.n0 = 3,
			.n1 = 4,
		};

		wgpuQueueWriteBuffer(mprg.queue, pp->gauss_unibuf, 0, &u, sizeof u);

		assert(ppw->swap_chain_texture_view);
		WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
			encoder,
			&(WGPURenderPassDescriptor){
				.colorAttachmentCount = 1,
				.colorAttachments = &(WGPURenderPassColorAttachment){
					.view = ppw->swap_chain_texture_view,
					.resolveTarget = 0,
					.loadOp = WGPULoadOp_Clear,
					.storeOp = WGPUStoreOp_Store,
					.clearValue = (WGPUColor){.r=1,.g=0,.b=1,.a=1},
				},
				.depthStencilAttachment = NULL,
			}
		);
		wgpuRenderPassEncoderSetPipeline(pass, pp->gauss_pipeline);

		WGPUBindGroup bind_group = ppw->fb[0].bind_group;
		assert(bind_group != NULL);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, 0);

		wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

		wgpuRenderPassEncoderEnd(pass);
		} break;
	}
}


#define FF_END_PASS         (1<<0)
#define FF_END_FRAME        (1<<1)
#define FF_VTXBUF_OVERFLOW  (1<<2)
#define FF_IDXBUF_OVERFLOW  (1<<3)
static void r_flush(int flags)
{
	assert(((flags == FF_END_PASS) || (flags == FF_END_FRAME) || ((flags&FF_VTXBUF_OVERFLOW) || (flags&FF_IDXBUF_OVERFLOW))) && "invalid r_flush() flags");

	struct r* r = &mprg.r;
	assert(mprg.vtxbuf_cursor >= r->cursor0);
	const int n = (mprg.vtxbuf_cursor - r->cursor0);

	const int is_end_pass         = flags & FF_END_PASS;
	const int is_end_frame        = flags & FF_END_FRAME;
	const int is_vtxbuf_overflow  = flags & FF_VTXBUF_OVERFLOW;
	const int is_idxbuf_overflow  = flags & FF_IDXBUF_OVERFLOW;

	assert((!is_end_frame || n == 0) && "cannot end frame with elements still in vtxbuf");

	const int do_pass   = (n > 0) && (is_end_pass || is_vtxbuf_overflow || is_idxbuf_overflow);
	const int do_submit = is_end_frame || is_vtxbuf_overflow;

	if (r->encoder == NULL) {
		r->encoder = wgpuDeviceCreateCommandEncoder(mprg.device, &(WGPUCommandEncoderDescriptor){});
		assert(r->encoder != NULL);
	}

	if (do_pass) {
		assert(r->mode > 0);
		assert(!is_end_frame && "not expecting to emit render passes during ''end frame''");
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

		assert(r->bind_group != NULL);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, r->bind_group, 0, 0);

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

	if (do_submit) {
		assert(r->encoder != NULL);

		// XXX assuming that wgpuRenderPassEncoderSetVertexBuffer()
		// doesn't do any buffer copying, so that it's fine to write
		// the entire vertex buffer just before submitting the queue.
		// if this is not the case, do wgpuQueueWriteBuffer() with
		// appropriate offset before setting the vertex buffer?
		if (mprg.vtxbuf_cursor > 0) {
			wgpuQueueWriteBuffer(mprg.queue, mprg.vtxbuf, 0, &mprg.vtxbuf_data, mprg.vtxbuf_cursor);
			mprg.vtxbuf_cursor = 0;
		}
		assert(mprg.vtxbuf_cursor == 0);

		if (is_end_frame) {
			postproc_end_frame(r->encoder);
		}

		WGPUCommandBuffer cmdbuf = wgpuCommandEncoderFinish(r->encoder, &(WGPUCommandBufferDescriptor){});
		wgpuQueueSubmit(mprg.queue, 1, &cmdbuf);
		r->encoder = NULL;
	}
}

static void r_begin_frames()
{
	struct r* r = &mprg.r;
	assert((!r->begun_frames) && "already inside r_begin_frames()");
	r->begun_frames = 1;
}

static void r_end_frames()
{
	struct r* r = &mprg.r;
	assert((r->begun_frames) && "not inside r_begin_frames()");

	// each r_begin_frame()/r_end_frame() cycle needs to update
	// window-local resources when postproc type changes. since
	// type!=previous_type is used to detect these changes, we can't update
	// previous_type until ALL windows have been rendered
	struct postproc* pp = &mprg.postproc;
	pp->previous_type = pp->type;

	r->begun_frames = 0;

	r->seed = fmodf(r->seed + 0.1f, 11.11f);
}

static void r_begin_frame(struct window* window, WGPUTextureView swap_chain_texture_view)
{
	struct r* r = &mprg.r;

	assert((r->begun_frames) && "not inside r_begin_frames()");
	assert((!r->begun_frame) && "already inside r_begin_frame()");

	window_update_size(window);

	assert((r->window == NULL) && "frame already begun");
	assert(mprg.vtxbuf_cursor == 0);

	r->n_passes = 0;
	assert(r->n_static_quad_indices == 0);
	r->window = window;
	r->render_target_texture_view = postproc_begin_frame(window, swap_chain_texture_view);

	enum postproc_type t = mprg.postproc.type;
	const float scalar =
		t == PP_NONE ? 16.0f :
		t == PP_GAUSS ? 1.0f :
		0.0f;

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
			.dst_dim = { window->width, window->height },
			.seed = r->seed,
			.scalar = scalar,
		};
		wgpuQueueWriteBuffer(mprg.queue, mprg.vector_unibuf, 0, &u, sizeof u);
	}

	r->begun_frame = 1;
}

static void r_end_frame()
{
	struct r* r = &mprg.r;
	assert((r->begun_frame) && "not inside r_begin_frame()");
	assert((r->mode == 0) && "mode not r_end()'d");
	assert((r->window != NULL) && "r_begin_frame() not called");
	r_flush(FF_END_FRAME);
	r->window = NULL;
	r->begun_frame = 0;
}

static void r_begin(int mode)
{
	struct r* r = &mprg.r;
	assert((r->begun_frame) && "not inside r_begin_frame()");
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
	r_flush(FF_END_PASS);
	struct r* r = &mprg.r;
	assert(r->mode > 0);
	r->mode = 0;
}

static void* r_request(size_t vtxbuf_requested, int idxbuf_requested)
{
	assert(((vtxbuf_requested&3) == 0) && "vtxbuf_requested must be 4-byte aligned");
	struct r* r = &mprg.r;
	const int vtxbuf_required = mprg.vtxbuf_cursor + vtxbuf_requested;
	const int idxbuf_required = r->n_static_quad_indices + idxbuf_requested;
	const int vtxbuf_overflow = (vtxbuf_required > VTXBUF_SZ);
	const int idxbuf_overflow = (idxbuf_required > STATIC_QUADS_IDXBUF_SZ);
	int ff =
		  (vtxbuf_overflow ? FF_VTXBUF_OVERFLOW : 0)
		+ (idxbuf_overflow ? FF_IDXBUF_OVERFLOW : 0);
	if (ff) r_flush(ff);
	assert((mprg.vtxbuf_cursor + vtxbuf_requested) <= VTXBUF_SZ);
	assert((r->n_static_quad_indices + idxbuf_requested ) <= STATIC_QUADS_IDXBUF_SZ);

	void* p = mprg.vtxbuf_data + mprg.vtxbuf_cursor;
	mprg.vtxbuf_cursor += vtxbuf_requested;
	assert((mprg.vtxbuf_cursor&3) == 0);

	r->n_static_quad_indices += idxbuf_requested;

	return p;
}

static uint16_t fto16(float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	return roundf(v * 65535.0f);
}

static void colpak(uint16_t* dst, union v4 rgba)
{
	const float s = 1.0f / 16.0f;
	dst[0] = fto16(rgba.s[0] * s);
	dst[1] = fto16(rgba.s[1] * s);
	dst[2] = fto16(rgba.s[2] * s);
	dst[3] = fto16(rgba.s[3]);
}

static void rv_quad(float x, float y, float w, float h, union v4 color)
{
	struct vector_vtx* pv = r_request(4 * sizeof(*pv), 6);

	pv[0].a_pos.x = x;
	pv[0].a_pos.y = y;
	colpak(pv[0].a_color, color);

	pv[1].a_pos.x = x+w;
	pv[1].a_pos.y = y;
	colpak(pv[1].a_color, color);

	pv[2].a_pos.x = x+w;
	pv[2].a_pos.y = y+h;
	colpak(pv[2].a_color, color);

	pv[3].a_pos.x = x;
	pv[3].a_pos.y = y+h;
	colpak(pv[3].a_color, color);
}

static void rv_quad_ygrad(float x, float y, float w, float h, union v4 color0, union v4 color1)
{
	struct vector_vtx* pv = r_request(4 * sizeof(*pv), 6);

	pv[0].a_pos.x = x;
	pv[0].a_pos.y = y;
	colpak(pv[0].a_color, color0);

	pv[1].a_pos.x = x+w;
	pv[1].a_pos.y = y;
	colpak(pv[1].a_color, color0);

	pv[2].a_pos.x = x+w;
	pv[2].a_pos.y = y+h;
	colpak(pv[2].a_color, color1);

	pv[3].a_pos.x = x;
	pv[3].a_pos.y = y+h;
	colpak(pv[3].a_color, color1);
}

static void wgpu_native_log_callback(WGPULogLevel level, const char* msg)
{
	const char* lvl =
		level == WGPULogLevel_Error ? "ERROR" :
		level == WGPULogLevel_Warn  ? "WARN"  :
		level == WGPULogLevel_Info  ? "INFO"  :
		level == WGPULogLevel_Debug ? "DEBUG" :
		level == WGPULogLevel_Trace ? "TRACE" :
		"???";
	printf("WGPU NATIVE [%s] :: %s\n", lvl, msg);
}

int main(int argc, char** argv)
{
	stm_setup();
	gpudl_init();

	wgpuSetLogCallback(wgpu_native_log_callback);
	//wgpuSetLogLevel(WGPULogLevel_Debug);
	//wgpuSetLogLevel(WGPULogLevel_Info);
	wgpuSetLogLevel(WGPULogLevel_Warn);

	new_window();

	WGPUAdapter adapter;
	WGPUDevice device;
	WGPUQueue queue;
	gpudl_get_wgpu(NULL, &adapter, &device, &queue);
	mprg.device = device;
	mprg.queue = queue;

#if 1
        WGPUAdapterProperties properties = {0};
        wgpuAdapterGetProperties(adapter, &properties);
        printf("vendor id: %d\n", properties.vendorID);
        printf("device id: %d\n", properties.deviceID);
        printf("adapter type: %d\n", properties.adapterType);
        printf("backend type: %d\n", properties.backendType);
        //printf("name: %s\n", properties.name);
        //printf("driver: %s\n", properties.driverDescription);

        WGPUSupportedLimits limits = {0};
        wgpuAdapterGetLimits(adapter, &limits);

        #define DUMP32(s) printf("  " #s ": %u\n", limits.limits.s);
        #define DUMP64(s) printf("  " #s ": %lu\n", limits.limits.s);
        DUMP32(maxTextureDimension1D)
        DUMP32(maxTextureDimension2D)
        DUMP32(maxTextureDimension3D)
        DUMP32(maxTextureArrayLayers)
        DUMP32(maxBindGroups)
        DUMP32(maxDynamicUniformBuffersPerPipelineLayout)
        DUMP32(maxDynamicStorageBuffersPerPipelineLayout)
        DUMP32(maxSampledTexturesPerShaderStage)
        DUMP32(maxSamplersPerShaderStage)
        DUMP32(maxStorageBuffersPerShaderStage)
        DUMP32(maxStorageTexturesPerShaderStage)
        DUMP32(maxUniformBuffersPerShaderStage)
        DUMP64(maxUniformBufferBindingSize)
        DUMP64(maxStorageBufferBindingSize)
        DUMP32(minUniformBufferOffsetAlignment)
        DUMP32(minStorageBufferOffsetAlignment)
        DUMP32(maxVertexBuffers)
        DUMP32(maxVertexAttributes)
        DUMP32(maxVertexBufferArrayStride)
        DUMP32(maxInterStageShaderComponents)
        DUMP32(maxComputeWorkgroupStorageSize)
        DUMP32(maxComputeInvocationsPerWorkgroup)
        DUMP32(maxComputeWorkgroupSizeX)
        DUMP32(maxComputeWorkgroupSizeY)
        DUMP32(maxComputeWorkgroupSizeZ)
        DUMP32(maxComputeWorkgroupsPerDimension)
        #undef DUMP64
        #undef DUMP32
        #endif

	mprg.postproc.type = PP_GAUSS;

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
						.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
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
									.format = WGPUVertexFormat_Unorm16x4,
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

	// PP_GAUSS
	{
		struct postproc* pp = &mprg.postproc;

		const size_t unisz = sizeof(struct ppgauss_uni);

		pp->gauss_unibuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
			.size = unisz,
		});
		assert(pp->gauss_unibuf);

		WGPUShaderModule shader = mk_shader_module(device, shadersrc_ppgauss);

		pp->gauss_bind_group_layout = wgpuDeviceCreateBindGroupLayout(
			device,
			&(WGPUBindGroupLayoutDescriptor){
				.entryCount = 3,
				.entries = (WGPUBindGroupLayoutEntry[]){
					(WGPUBindGroupLayoutEntry){
						.binding = 0,
						.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
						.buffer = (WGPUBufferBindingLayout){
							.type = WGPUBufferBindingType_Uniform,
							.hasDynamicOffset = false,
							.minBindingSize = unisz,
						},
					},
					(WGPUBindGroupLayoutEntry){
						.binding = 1,
						.visibility = WGPUShaderStage_Fragment,
						.texture = (WGPUTextureBindingLayout){
							.sampleType = WGPUTextureSampleType_Float,
							.viewDimension = WGPUTextureViewDimension_2D,
							.multisampled = false,
						},
					},
					(WGPUBindGroupLayoutEntry){
						.binding = 2,
						.visibility = WGPUShaderStage_Fragment,
						.sampler = (WGPUSamplerBindingLayout){
							.type = WGPUSamplerBindingType_Filtering,
						},
					},
				},
			}
		);
		assert(pp->gauss_bind_group_layout);

		pp->gauss_sampler = wgpuDeviceCreateSampler(mprg.device, &(WGPUSamplerDescriptor) {
			.addressModeU = WGPUAddressMode_ClampToEdge,
			.addressModeV = WGPUAddressMode_ClampToEdge,
			.addressModeW = WGPUAddressMode_ClampToEdge,
			.magFilter = WGPUFilterMode_Linear,
			.minFilter = WGPUFilterMode_Linear,
			.mipmapFilter = WGPUMipmapFilterMode_Nearest,
			.lodMinClamp = 0.0f,
			.lodMaxClamp = 0.0f,
		});
		assert(pp->gauss_sampler);

		WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
			device,
			&(WGPUPipelineLayoutDescriptor){
				.bindGroupLayoutCount = 1,
				.bindGroupLayouts = (WGPUBindGroupLayout[]){
					pp->gauss_bind_group_layout,
				},
			}
		);
		assert(pipeline_layout);

		pp->gauss_pipeline = wgpuDeviceCreateRenderPipeline(
			device,
			&(WGPURenderPipelineDescriptor){
				.layout = pipeline_layout,
				.vertex = (WGPUVertexState){
					.module = shader,
					.entryPoint = "vs_main",
					.bufferCount = 0, // using @builtin(vertex_index)
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
						.format = FRAMEBUFFER_DEFAULT_TEXTURE_FORMAT,
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
		assert(pp->gauss_pipeline);
	}

	struct fps* fps = fps_new(60);

	int imax = 16;
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
				if (e.key.pressed) {
					if (e.key.code == '\033') do_close = 1;
					if (e.key.code == 'p') {
						if (mprg.postproc.type == PP_GAUSS) {
							mprg.postproc.type = PP_NONE;
						} else {
							mprg.postproc.type = PP_GAUSS;
						}
					}
					if (e.key.code == GK_UP) imax++;
					if (e.key.code == GK_DOWN) imax--;
					if (imax < 0) imax = 0;
					if (imax > 16) imax = 16;

				}
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
		r_begin_frames();
		for (int i = 0; i < mprg.n_windows; i++) {
			struct window* window = &mprg.windows[i];
			WGPUTextureView v = gpudl_render_begin(window->id);
			if (!v) continue;

			r_begin_frame(window, v);

			r_begin(R_MODE_VECTOR);

			const float m1 = imax;;
			{
				int x0 = 200;
				int inc = 200;
				const float m0 = 1;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m0,m0,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m0,m1,m0,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m0,m0,m1,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m0,m1,m1,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m0,m1,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m1,m0,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m1,m1,1), v4(0,0,0,0));
				x0 += inc;
			}

			{
				int y = 0;
				int inc = 10;
				while (y < window->height) {
					rv_quad(0, y, window->width, inc/5, v4(0,0,0,0));
					y += inc;
					inc += 10;
				}
			}

			rv_quad_ygrad(80, 0, 5, window->height, v4(m1,m1,m1,1), v4(0,0,0,0));

			r_end();

			r_end_frame();

			gpudl_render_end();
		}
		r_end_frames();

		if (fps_frame(fps)) printf("fps: %.2f\n", fps->fps);
	}

	return EXIT_SUCCESS;
}
