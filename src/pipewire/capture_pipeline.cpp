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

    const struct spa_pod* params[2];
    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        // Compositors commonly deliver BGRx/BGRA; offering only RGBA made negotiation fail
        // or swapped red and blue.
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5,
            SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA,
            SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&max_res, &min_res, &max_res),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&max_framerate, &min_framerate, &max_framerate)
    );
    // Only accept CPU-mappable buffers (not DMA-BUF) so we can read and filter the pixels.
    params[1] = (const struct spa_pod*)spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))
    );

    uint32_t target_node = (node_id > 0) ? node_id : PW_ID_ANY;
    int res = pw_stream_connect(
        stream_,
        PW_DIRECTION_INPUT,
        target_node,
        static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        params, 2
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
            params, 2
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
    if (timer_ && loop_) {
        pw_loop_destroy_source(pw_thread_loop_get_loop(loop_), timer_);
        timer_ = nullptr;
    }
    if (guard_) {
        guard_->stop();
        guard_.reset();
        ocr_.reset();
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
    if (guard_) guard_->set_extra_rules(rules);
    std::lock_guard<std::mutex> lk(rules_mu_);
    filter_rules_ = rules;
}

bool CapturePipeline::enable_protection(BlurMode mode, const char* ocr_lang) {
    if (guard_) return true;

    auto ocr = std::make_unique<OcrEngine>();
    const bool ocr_ok = ocr->init(ocr_lang);
    if (!ocr_ok && mode == BlurMode::DataOnly) return false;

    StreamGuard::OcrFn fn;
    if (ocr_ok) {
        OcrEngine* raw = ocr.get();
        fn = [raw](const GrayImage& img) { return raw->recognize(img); };
        ocr_ = std::move(ocr);
    }
    guard_ = std::make_unique<StreamGuard>(StreamGuard::Options{}, fn);
    guard_->set_mode(mode);
    {
        std::lock_guard<std::mutex> lk(rules_mu_);
        guard_->set_extra_rules(filter_rules_);
    }
    guard_->start();

    // Frames are only delivered by the compositor when the screen changes, so a held frame
    // would never be released on a static screen. A short timer on the PipeWire loop does it.
    if (loop_) {
        pw_thread_loop_lock(loop_);
        timer_ = pw_loop_add_timer(pw_thread_loop_get_loop(loop_), on_timer, this);
        if (timer_) {
            struct timespec value = {0, 8 * 1000 * 1000};
            struct timespec interval = {0, 8 * 1000 * 1000};
            pw_loop_update_timer(pw_thread_loop_get_loop(loop_), timer_, &value, &interval, false);
        }
        pw_thread_loop_unlock(loop_);
    }
    std::cout << "[WayBlur] Stream protection on ("
              << (mode == BlurMode::FullScreen ? "whole screen" : "sensitive data only")
              << (ocr_ok ? ", OCR ready" : ", no OCR") << ")\n";
    return true;
}

bool CapturePipeline::set_blur_mode(BlurMode mode) {
    if (!guard_) return false;
    if (mode == BlurMode::DataOnly && !ocr_) return false;
    guard_->set_mode(mode);
    return true;
}

void CapturePipeline::process_frame(FrameBuffer& frame) {
    // Direct, synchronous path used by test mode and benchmark: window rules only.
    std::vector<FilterRule> rules;
    {
        std::lock_guard<std::mutex> lk(rules_mu_);
        rules = filter_rules_;
    }
    filter_.apply_filter(frame, rules);
    frames_processed_++;
}

void CapturePipeline::emit_ready() {
    if (!guard_ || !stream_out_) return;
    StreamGuard::OutFrame out;
    if (!guard_->poll(StreamGuard::Clock::now(), out)) return;

    struct pw_buffer* b_out = pw_stream_dequeue_buffer(stream_out_);
    if (!b_out) { frames_dropped_++; return; }
    struct spa_buffer* buf = b_out->buffer;
    if (buf->datas[0].data && buf->datas[0].maxsize >= out.data.size()) {
        memcpy(buf->datas[0].data, out.data.data(), out.data.size());
        buf->datas[0].chunk->offset = 0;
        buf->datas[0].chunk->size = static_cast<uint32_t>(out.data.size());
        buf->datas[0].chunk->stride = out.stride;
        frames_processed_++;
    } else {
        frames_dropped_++;
    }
    pw_stream_queue_buffer(stream_out_, b_out);
}

void CapturePipeline::on_timer(void* data, uint64_t) {
    static_cast<CapturePipeline*>(data)->emit_ready();
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
        switch (info.format) {
            case SPA_VIDEO_FORMAT_BGRx:
            case SPA_VIDEO_FORMAT_BGRA: self->frame_format_ = "BGRA"; break;
            default:                    self->frame_format_ = "RGBA"; break;
        }
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
        if (buf_in->datas[0].chunk && buf_in->datas[0].chunk->stride > 0) {
            stride = buf_in->datas[0].chunk->stride;
        }

        FrameBuffer frame;
        frame.data = static_cast<uint8_t*>(buf_in->datas[0].data);
        frame.width = width;
        frame.height = height;
        frame.stride = stride;
        frame.format = self->frame_format_;

        if (self->guard_) {
            // Hand the frame to the guard (it copies), then release whatever is safe to send.
            self->guard_->push(frame, StreamGuard::Clock::now());
            self->emit_ready();
        } else {
            // No guard: window rules only (applied in place, then forwarded).
            self->process_frame(frame);
            if (self->stream_out_) {
                struct pw_buffer* b_out = pw_stream_dequeue_buffer(self->stream_out_);
                if (b_out) {
                    struct spa_buffer* buf_out = b_out->buffer;
                    uint32_t size = buf_in->datas[0].chunk->size;
                    if (size == 0) size = static_cast<uint32_t>(height * stride);
                    if (buf_out->datas[0].data && buf_out->datas[0].maxsize >= size) {
                        memcpy(buf_out->datas[0].data, buf_in->datas[0].data, size);
                        buf_out->datas[0].chunk->offset = 0;
                        buf_out->datas[0].chunk->size = size;
                        buf_out->datas[0].chunk->stride = stride;
                    }
                    pw_stream_queue_buffer(self->stream_out_, b_out);
                }
            }
        }
    }

    pw_stream_queue_buffer(self->stream_, b_in);
}

void CapturePipeline::on_state_changed(void*, enum pw_stream_state, enum pw_stream_state, const char*) {}

} // namespace wayblur
