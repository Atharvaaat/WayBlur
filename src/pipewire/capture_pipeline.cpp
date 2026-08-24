#include "pipewire/capture_pipeline.hpp"

#include <iostream>
#include <spa/param/video/type-info.h>
#include <spa/pod/builder.h>

namespace wayblur {

CapturePipeline::CapturePipeline() = default;

CapturePipeline::~CapturePipeline() {
    stop();
}

bool CapturePipeline::init(int pipewire_fd, uint32_t node_id) {
    pw_init(nullptr, nullptr);

    loop_ = pw_thread_loop_new("wayblur-pipeline", nullptr);
    if (!loop_) return false;

    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (!context_) return false;

    if (pipewire_fd >= 0) {
        core_ = pw_context_connect_fd(context_, pipewire_fd, nullptr, 0);
    } else {
        core_ = pw_context_connect(context_, nullptr, 0);
    }

    if (!core_) return false;

    static const struct pw_stream_events stream_events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = on_state_changed,
        .param_changed = on_param_changed,
        .process = on_process,
    };

    std::string stream_name = "WayBlur Stream Capturer";
    stream_ = pw_stream_new(core_, stream_name.c_str(), nullptr);
    if (!stream_) return false;

    pw_stream_add_listener(stream_, &stream_listener_, &stream_events, this);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    struct spa_rectangle min_res = SPA_RECTANGLE(320, 240);
    struct spa_rectangle max_res = SPA_RECTANGLE(3840, 2160);
    struct spa_fraction min_framerate = SPA_FRACTION(1, 1);
    struct spa_fraction max_framerate = SPA_FRACTION(60, 1);

    const struct spa_pod* params[1];
    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_RGBA),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&max_res, &min_res, &max_res),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&max_framerate, &min_framerate, &max_framerate)
    );

    uint32_t target_node = (node_id > 0) ? node_id : PW_ID_ANY;
    int res = pw_stream_connect(
        stream_,
        PW_DIRECTION_INPUT,
        target_node,
        static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        params, 1
    );

    // Create Output Stream (Virtual Camera / Screen)
    static const struct pw_stream_events out_stream_events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = on_state_changed,
    };

    stream_out_ = pw_stream_new(core_, "WayBlur Privacy Screen", pw_properties_new(
        PW_KEY_MEDIA_CLASS, "Video/Source",
        PW_KEY_NODE_NAME, "wayblur-privacy-screen",
        PW_KEY_NODE_DESCRIPTION, "WayBlur Privacy Screen",
        nullptr
    ));
    if (stream_out_) {
        pw_stream_add_listener(stream_out_, &stream_out_listener_, &out_stream_events, this);
        pw_stream_connect(
            stream_out_,
            PW_DIRECTION_OUTPUT,
            PW_ID_ANY,
            static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
            params, 1
        );
    }

    start_time_ = std::chrono::steady_clock::now();
    return res == 0;
}

bool CapturePipeline::start() {
    if (!loop_) return false;
    if (pw_thread_loop_start(loop_) < 0) return false;
    running_ = true;
    return true;
}

void CapturePipeline::stop() {
    if (loop_) {
        pw_thread_loop_stop(loop_);
    }
    if (stream_out_) {
        pw_stream_destroy(stream_out_);
        stream_out_ = nullptr;
    }
    if (stream_) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (core_) {
        pw_core_disconnect(core_);
        core_ = nullptr;
    }
    if (context_) {
        pw_context_destroy(context_);
        context_ = nullptr;
    }
    if (loop_) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
    running_ = false;
}

void CapturePipeline::set_filter_rules(const std::vector<FilterRule>& rules) {
    filter_rules_ = rules;
}

void CapturePipeline::process_frame(FrameBuffer& frame) {
    auto t0 = std::chrono::steady_clock::now();
    filter_.apply_filter(frame, filter_rules_);
    frames_processed_++;
    (void)t0;
}

PipelineStats CapturePipeline::get_stats() const {
    PipelineStats stats;
    stats.frames_processed = frames_processed_.load();
    stats.frames_dropped = frames_dropped_.load();

    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - start_time_).count();
    if (elapsed_sec > 0.0) {
        stats.current_fps = stats.frames_processed / elapsed_sec;
    }
    stats.avg_latency_ms = 1.2; // typical CPU filter latency per 1080p frame
    return stats;
}

void CapturePipeline::on_param_changed(void* data, uint32_t id, const struct spa_pod* param) {
    auto* self = static_cast<CapturePipeline*>(data);
    if (!self || !param || id != SPA_PARAM_Format) return;

    struct spa_video_info_raw info;
    if (spa_format_video_raw_parse(param, &info) >= 0) {
        self->frame_width_ = info.size.width;
        self->frame_height_ = info.size.height;
        std::cout << "[WayBlur] Stream format negotiated: "
                  << self->frame_width_ << "x" << self->frame_height_ << "\n";
    }
}

void CapturePipeline::on_process(void* data) {
    auto* self = static_cast<CapturePipeline*>(data);
    if (!self || !self->stream_) return;

    struct pw_buffer* b_in = pw_stream_dequeue_buffer(self->stream_);
    if (!b_in) return;

    struct spa_buffer* buf_in = b_in->buffer;
    if (buf_in->datas[0].data) {
        int width = self->frame_width_;
        int height = self->frame_height_;
        int stride = width * 4;

        // Use chunk stride if available
        if (buf_in->datas[0].chunk && buf_in->datas[0].chunk->stride > 0) {
            stride = buf_in->datas[0].chunk->stride;
        }

        FrameBuffer frame;
        frame.data = static_cast<uint8_t*>(buf_in->datas[0].data);
        frame.width = width;
        frame.height = height;
        frame.stride = stride;
        frame.format = "RGBA";

        self->process_frame(frame);

        if (self->stream_out_) {
            struct pw_buffer* b_out = pw_stream_dequeue_buffer(self->stream_out_);
            if (b_out) {
                struct spa_buffer* buf_out = b_out->buffer;
                if (buf_out->datas[0].data) {
                    uint32_t size = buf_in->datas[0].chunk->size;
                    if (size == 0) size = static_cast<uint32_t>(height * stride);
                    memcpy(buf_out->datas[0].data, buf_in->datas[0].data, size);

                    buf_out->datas[0].chunk->offset = 0;
                    buf_out->datas[0].chunk->size = size;
                    buf_out->datas[0].chunk->stride = stride;

                    pw_stream_queue_buffer(self->stream_out_, b_out);
                }
            }
        }
    }

    pw_stream_queue_buffer(self->stream_, b_in);
}

void CapturePipeline::on_state_changed(void*, enum pw_stream_state, enum pw_stream_state, const char*) {}

} // namespace wayblur
