// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "audio_waveform_scanner.hpp"
#include <vlc_aout.h>
#include <vlc_block.h>
#include <vlc_codec.h>
#include <vlc_demux.h>
#include <vlc_es_out.h>
#include <vlc_stream.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

bool canScanAudioWaveform(const QUrl& url, vlc_tick_t duration)
{
    // Qt also uses file: URLs for Windows UNC paths and mapped shares.
    return url.isLocalFile() && duration > 0;
}

namespace {
struct Output;
struct AudioDecoder
{
    decoder_t decoder;
    es_format_t input;
    Output* output;
    aout_filters_t* filters;
    audio_sample_format_t format;
};
struct Track
{
    es_format_t format;
    bool selected;
    AudioDecoder* decoder = nullptr;
    bool drained = false;
    Track(const es_format_t& source, bool selected) : selected(selected) { es_format_Copy(&format, &source); }
    ~Track() {
        if (decoder) {
            decoder_Clean(&decoder->decoder);
            if (decoder->filters) aout_FiltersDelete(&decoder->decoder, decoder->filters);
            es_format_Clean(&decoder->input);
            vlc_object_delete(&decoder->decoder);
        }
        es_format_Clean(&format);
    }
};
struct Output : es_out_t
{
    vlc_object_t* parent;
    const AudioWaveformSource& source;
    std::vector<std::unique_ptr<Track>> tracks;
    QVector<float> peaks;
    vlc_tick_t duration;
    int audioCount = 0;
    bool selectedFound = false;
    std::atomic_bool failed{false};
    bool receivedAudio = false;
    std::mutex mutex;

    Output(vlc_object_t* parent, const AudioWaveformSource& source);
    void collect(AudioDecoder* decoder, block_t* block) {
        std::lock_guard<std::mutex> lock(mutex);
        if (vlc_killed() || failed) { block_Release(block); return; }
        if (!decoder->filters) { failed = true;block_Release(block);return; }
        block = aout_FiltersPlay(decoder->filters, block, 1.f);
        if (!block) return;
        const auto channels = decoder->format.i_channels;
        const auto rate = decoder->format.i_rate;
        const auto count = std::min<size_t>(block->i_nb_samples, channels ? block->i_buffer / (sizeof(float) * channels) : 0);
        if (!channels || !rate || duration <= 0 || block->i_pts < VLC_TICK_0) {
            block_Release(block); return;
        }
        const vlc_tick_t start = block->i_pts - VLC_TICK_0;
        for (size_t frame = 0; frame < count; ++frame) {
            const vlc_tick_t time = start + vlc_tick_from_samples(frame, rate);
            if (time < 0 || time >= duration) continue;
            const int bin = std::min<int>(peaks.size() - 1, (long double)time * peaks.size() / duration);
            float peak = peaks[bin];
            for (unsigned channel = 0; channel < channels; ++channel) {
                float sample;
                memcpy(&sample, block->p_buffer + (frame * channels + channel) * sizeof(float), sizeof(sample));
                if (std::isfinite(sample)) peak = std::max(peak, std::min(1.f, std::abs(sample)));
            }
            peaks[bin] = peak;
            receivedAudio = true;
        }
        block_Release(block);
    }
    bool createDecoder(Track& track) {
        auto* owner = static_cast<AudioDecoder*>(vlc_object_create(parent, sizeof(AudioDecoder)));
        if (!owner) return false;
        owner->output = this;
        owner->filters = nullptr;
        decoder_Init(&owner->decoder, &owner->input, &track.format);
        static const decoder_owner_callbacks callbacks = [] {
            decoder_owner_callbacks callbacks{};
            callbacks.audio.format_update = [](decoder_t* decoder) {
                auto* owner = reinterpret_cast<AudioDecoder*>(decoder);
                std::lock_guard<std::mutex> lock(owner->output->mutex);
                if (owner->filters) { aout_FiltersDelete(decoder, owner->filters); owner->filters = nullptr; }
                // As the core's decoder owner does: a decoder such as FLAC
                // sizes its buffers (decoder_NewAudioBuffer) from these.
                decoder->fmt_out.audio.i_format = decoder->fmt_out.i_codec;
                auto input = decoder->fmt_out.audio;
                aout_FormatPrepare(&input);
                if (!input.i_bytes_per_frame || !input.i_frame_length) return VLC_EGENERIC;
                decoder->fmt_out.audio.i_bytes_per_frame = input.i_bytes_per_frame;
                decoder->fmt_out.audio.i_bitspersample = input.i_bitspersample;
                decoder->fmt_out.audio.i_frame_length = input.i_frame_length;
                owner->format = input;
                owner->format.i_format = VLC_CODEC_FL32;
                owner->format.channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
                aout_FormatPrepare(&owner->format);
                if (!input.i_channels || !input.i_rate || input.i_channels > AOUT_CHAN_MAX) return VLC_EGENERIC;
                owner->filters = aout_FiltersNew(decoder, &input, &owner->format, nullptr);
                return owner->filters ? VLC_SUCCESS : VLC_EGENERIC;
            };
            callbacks.audio.queue = [](decoder_t* decoder, block_t* block) {
                auto* owner = reinterpret_cast<AudioDecoder*>(decoder);
                owner->output->collect(owner, block);
            };
            return callbacks;
        }();
        owner->decoder.cbs = &callbacks;
        if (decoder_LoadModule(&owner->decoder, false, true) != VLC_SUCCESS) {
            decoder_Clean(&owner->decoder);
            if (owner->filters) aout_FiltersDelete(&owner->decoder, owner->filters);
            es_format_Clean(&owner->input);
            vlc_object_delete(&owner->decoder);
            return false;
        }
        track.decoder = owner;
        return true;
    }
    void drain(Track& track) {
        if (track.decoder && !track.drained) {
            track.decoder->decoder.pf_decode(&track.decoder->decoder, nullptr);
            track.drained = true;
        }
    }
};

Output::Output(vlc_object_t* parent, const AudioWaveformSource& source)
    : parent(parent), source(source), peaks(source.bins, 0.f), duration(source.duration)
{
    static const es_out_callbacks callbacks = [] {
        es_out_callbacks callbacks{};
        callbacks.add = [](es_out_t* base, input_source_t*, const es_format_t* format) -> es_out_id_t* {
            auto* output = static_cast<Output*>(base);
            const bool selected = format->i_cat == AUDIO_ES && output->audioCount++ == output->source.audioIndex;
            auto track = std::make_unique<Track>(*format, selected);
            auto* id = reinterpret_cast<es_out_id_t*>(track.get());
            output->selectedFound |= selected;
            output->tracks.push_back(std::move(track));
            return id;
        };
        callbacks.send = [](es_out_t* base, es_out_id_t* id, block_t* packet) {
            auto* output = static_cast<Output*>(base);
            auto* track = reinterpret_cast<Track*>(id);
            if (!track->selected || output->failed || vlc_killed()) { block_Release(packet); return VLC_SUCCESS; }
            if (!track->decoder && !output->createDecoder(*track)) {
                output->failed = true; block_Release(packet); return VLC_EGENERIC;
            }
            const int status = track->decoder->decoder.pf_decode(&track->decoder->decoder, packet);
            if (status == VLCDEC_RELOAD) { block_Release(packet); output->failed = true; }
            else if (status != VLCDEC_SUCCESS) output->failed = true;
            return output->failed ? VLC_EGENERIC : VLC_SUCCESS;
        };
        callbacks.del = [](es_out_t* base, es_out_id_t* id) {
            auto* output = static_cast<Output*>(base);auto* track = reinterpret_cast<Track*>(id);
            const auto found = std::find_if(output->tracks.begin(), output->tracks.end(), [track](const auto& item) { return item.get() == track; });
            if (found != output->tracks.end()) { output->drain(**found); output->tracks.erase(found); }
        };
        callbacks.control = [](es_out_t*, input_source_t*, int query, va_list args) {
            switch (query) {
            case ES_OUT_GET_ES_STATE: {
                auto* track = reinterpret_cast<Track*>(va_arg(args, es_out_id_t*));
                *va_arg(args, bool*) = track && track->selected;return VLC_SUCCESS;
            }
            case ES_OUT_IS_EMPTY: *va_arg(args, bool*) = true;return VLC_SUCCESS;
            case ES_OUT_SET_ES_FMT: {
                auto* track = reinterpret_cast<Track*>(va_arg(args, es_out_id_t*));
                const auto* format = va_arg(args, const es_format_t*);
                if (!track || track->decoder) return VLC_EGENERIC;
                es_format_Clean(&track->format);return es_format_Copy(&track->format, format);
            }
            case ES_OUT_SET_ES: case ES_OUT_UNSET_ES: case ES_OUT_SET_ES_DEFAULT:
            case ES_OUT_SET_ES_STATE: case ES_OUT_SET_ES_CAT_POLICY: case ES_OUT_SET_GROUP:
            case ES_OUT_SET_PCR: case ES_OUT_SET_GROUP_PCR: case ES_OUT_RESET_PCR:
            case ES_OUT_SET_NEXT_DISPLAY_TIME: case ES_OUT_SET_GROUP_META: case ES_OUT_SET_META:
                return VLC_SUCCESS;
            default:return VLC_EGENERIC;
            }
        };
        callbacks.destroy = [](es_out_t*) {};
        return callbacks;
    }();
    cbs = &callbacks;
}
}

AudioWaveformScan scanAudioWaveform(vlc_object_t* parent, const AudioWaveformSource& source,
                                    vlc_interrupt_t* interrupt)
{
    AudioWaveformScan result;
    auto* previous = vlc_interrupt_set(interrupt);
    if (vlc_killed()) { vlc_interrupt_set(previous);result.status = AudioWaveformScan::Cancelled;return result; }
    if (source.bins <= 0 || source.bins > 16384 || source.audioIndex < 0
        || (source.deadline != VLC_TICK_INVALID && vlc_tick_now() >= source.deadline)) {
        vlc_interrupt_set(previous);result.status = AudioWaveformScan::LimitReached;return result;
    }
    auto* context = static_cast<vlc_object_t*>(vlc_object_create(parent, sizeof(vlc_object_t)));
    if (!context) { vlc_interrupt_set(previous);return result; }
    context->no_interact = true;
    var_Create(context, "audio-filter", VLC_VAR_STRING);var_SetString(context, "audio-filter", "");
    var_Create(context, "audio-visual", VLC_VAR_STRING);var_SetString(context, "audio-visual", "none");
    var_Create(context, "audio-time-stretch", VLC_VAR_BOOL);var_SetBool(context, "audio-time-stretch", false);
    var_Create(context, "interact", VLC_VAR_BOOL);var_SetBool(context, "interact", false);
    {
        Output output(context, source);
        stream_t* stream = vlc_stream_NewURL(context, source.uri.constData());
        if (stream) {
            auto* demux = demux_New(context, "any", source.uri.constData(), stream, &output);
            if (demux) {
                if (output.duration <= 0) demux_Control(demux, DEMUX_GET_LENGTH, &output.duration);
                int status = VLC_DEMUXER_SUCCESS;
                bool expired = false;
                while (status > 0 && !vlc_killed() && !output.failed && output.duration > 0) {
                    if (source.deadline != VLC_TICK_INVALID && vlc_tick_now() >= source.deadline) { expired = true;break; }
                    status = demux_Demux(demux);
                }
                for (auto& track : output.tracks) output.drain(*track);
                demux_Delete(demux);output.tracks.clear();
                result.status = vlc_killed() ? AudioWaveformScan::Cancelled
                    : expired ? AudioWaveformScan::LimitReached
                    : output.failed || status < 0 || output.duration <= 0 ? AudioWaveformScan::Failed
                    : !output.selectedFound ? AudioWaveformScan::TrackMissing
                    : !output.receivedAudio ? AudioWaveformScan::Failed : AudioWaveformScan::Complete;
                if (result.status == AudioWaveformScan::Complete) result.peaks = std::move(output.peaks);
            } else vlc_stream_Delete(stream);
        }
        if (vlc_killed()) result.status = AudioWaveformScan::Cancelled;
    }
    vlc_object_delete(context);vlc_interrupt_set(previous);
    return result;
}

ProgressiveAudioWaveform::ProgressiveAudioWaveform(vlc_tick_t duration, int bins)
    : m_duration(duration), m_peaks(std::clamp(bins, 1, 16384), -1.f) {}

void ProgressiveAudioWaveform::append(vlc_tick_t time, double rate,
                                      const audio_sample_format_t& format,
                                      const block_t& block)
{
    if (m_duration <= 0 || !format.i_channels || !format.i_rate || time < 0
        || !std::isfinite(rate) || rate <= 0) return;
    unsigned bytes;
    switch (format.i_format) {
    case VLC_CODEC_FL64: bytes = 8; break;
    case VLC_CODEC_FL32: case VLC_CODEC_S32N: bytes = 4; break;
    case VLC_CODEC_S16N: bytes = 2; break;
    case VLC_CODEC_U8: case VLC_CODEC_S8: bytes = 1; break;
    default: return; // Passthrough and non-PCM formats have no waveform.
    }
    const size_t frames = std::min<size_t>(block.i_nb_samples, block.i_buffer / bytes / format.i_channels);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (size_t frame = 0; frame < frames; ++frame) {
        const long double position = time + (long double)vlc_tick_from_samples(frame, format.i_rate) * rate;
        if (position >= m_duration) break;
        const int bin = std::min<int>(m_peaks.size() - 1, position * m_peaks.size() / m_duration);
        float peak = std::max(0.f, m_peaks[bin]);
        for (unsigned channel = 0; channel < format.i_channels; ++channel) {
            const auto* sample = block.p_buffer + (frame * format.i_channels + channel) * bytes;
            double value;
            switch (format.i_format) {
            case VLC_CODEC_FL64: { double v; memcpy(&v, sample, 8); value = v; break; }
            case VLC_CODEC_FL32: { float v; memcpy(&v, sample, 4); value = v; break; }
            case VLC_CODEC_S32N: { int32_t v; memcpy(&v, sample, 4); value = v / 2147483648.; break; }
            case VLC_CODEC_S16N: { int16_t v; memcpy(&v, sample, 2); value = v / 32768.; break; }
            case VLC_CODEC_S8: value = *(const int8_t*)sample / 128.; break;
            default: value = (*sample - 128) / 128.; break;
            }
            if (std::isfinite(value)) peak = std::max(peak, float(std::min(1., std::abs(value))));
        }
        m_peaks[bin] = peak;
    }
}

QVector<float> ProgressiveAudioWaveform::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_peaks;
}

bool ProgressiveAudioWaveform::restore(const QVector<float>& peaks)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (peaks.size() != m_peaks.size()) return false;
    for (float value : peaks) if (!std::isfinite(value) || value < -1.f || value > 1.f) return false;
    m_peaks = peaks;
    return true;
}
