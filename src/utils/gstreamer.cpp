/**
    gTox a GTK-based tox-client - https://github.com/KoKuToru/gTox.git

    Copyright (C) 2015  Luca Béla Palkovics

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>
**/
#include "gstreamer.h"
#include <gstreamermm/bus.h>
#include <glibmm/i18n.h>
#include <gstreamermm/uridecodebin.h>
#include <future>

#ifndef SIGC_CPP11_HACK
#define SIGC_CPP11_HACK
namespace sigc {
    SIGC_FUNCTORS_DEDUCE_RESULT_TYPE_WITH_DECLTYPE
}
#endif

using namespace utils;

auto gstreamer::signal_error() -> type_signal_error {
    return m_signal_error;
}

auto gstreamer::property_uri()      -> Glib::PropertyProxy<Glib::ustring> {
    return m_property_uri.get_proxy();
}
auto gstreamer::property_state()    -> Glib::PropertyProxy<Gst::State> {
    return m_property_state.get_proxy();
}
auto gstreamer::property_position() -> Glib::PropertyProxy_ReadOnly<gint64> {
    return Glib::PropertyProxy_ReadOnly<gint64>(this, "gstreamer-position");
}
auto gstreamer::property_duration() -> Glib::PropertyProxy_ReadOnly<gint64> {
    return Glib::PropertyProxy_ReadOnly<gint64>(this, "gstreamer-duration");
}
auto gstreamer::property_volume()   -> Glib::PropertyProxy<double> {
    return m_property_volume.get_proxy();
}
auto gstreamer::property_pixbuf()   -> Glib::PropertyProxy_ReadOnly<Glib::RefPtr<Gdk::Pixbuf>> {
    return Glib::PropertyProxy_ReadOnly<Glib::RefPtr<Gdk::Pixbuf>>(this, "gstreamer-pixbuf");
}

void gstreamer::init() {
    utils::debug::scope_log log(DBG_LVL_1("gtox"), {});

    m_playbin = Gst::PlayBin::create();
    m_appsink = Gst::AppSink::create();

    m_appsink->property_caps() = Gst::Caps::create_from_string("video/x-raw,format=RGB,pixel-aspect-ratio=1/1");
    m_appsink->property_max_buffers() = 3;
    m_appsink->property_drop() = true;
    m_appsink->property_emit_signals() = true;

    utils::dispatcher::ref weak_dispatcher = m_dispatcher;
    auto resolution = std::make_shared<std::pair<int, int>>();

    m_appsink->signal_new_preroll().connect(sigc::track_obj([this, sink = m_appsink, weak_dispatcher, resolution, prev_dispatched = sigc::connection()]() mutable {
        utils::debug::scope_log log(DBG_LVL_5("gtox"), {});
        auto preroll = sink->pull_preroll();
        if (!preroll) {
            return Gst::FLOW_OK;
        }
        //C++ version is buggy, use C version
        auto caps = gst_sample_get_caps(preroll->gobj());
        if (!caps) {
            return Gst::FLOW_OK;
        }
        if (gst_caps_is_empty(caps)) {
            return Gst::FLOW_OK;
        }

        auto struc = gst_caps_get_structure(caps, 0); // < can this fail ?
        int w,h;
        if (!gst_structure_get_int(struc, "width", &w)) {
            return Gst::FLOW_OK;
        }
        if (!gst_structure_get_int(struc, "height", &h)) {
            return Gst::FLOW_OK;
        }
        resolution->first = w;
        resolution->second = h;

        auto frame = extract_frame(preroll, resolution);
        prev_dispatched.disconnect();
        prev_dispatched = weak_dispatcher.emit([this, frame]() {
            utils::debug::scope_log log(DBG_LVL_5("gtox"), {});
            gint64 pos, dur;
            if (m_playbin
                && m_playbin->query_position(Gst::FORMAT_TIME, pos)
                && m_playbin->query_duration(Gst::FORMAT_TIME, dur)) {
                //set
                m_property_duration.set_value(dur);
                m_property_position.set_value(pos);
            }
            m_property_pixbuf.set_value(frame);
        });
        return Gst::FLOW_OK;

    }, *this));
    m_appsink->signal_new_sample().connect(sigc::track_obj([this, sink = m_appsink, weak_dispatcher, resolution, prev_dispatched = sigc::connection()]() mutable {
        utils::debug::scope_log log(DBG_LVL_5("gtox"), {});
        auto sample = sink->pull_sample();
        auto frame = extract_frame(sample, resolution);
        prev_dispatched.disconnect();
        prev_dispatched = weak_dispatcher.emit([this, frame]() {
            utils::debug::scope_log log(DBG_LVL_5("gtox"), {});
            gint64 pos, dur;
            if (m_playbin
                && m_playbin->query_position(Gst::FORMAT_TIME, pos)
                && m_playbin->query_duration(Gst::FORMAT_TIME, dur)) {
                //set
                m_property_duration.set_value(dur);
                m_property_position.set_value(pos);
            }
            m_property_pixbuf.set_value(frame);
        });
        return Gst::FLOW_OK;
    }, *this));

    m_playbin->property_video_sink() = m_appsink;

    m_playbin->get_bus()->add_watch(
                sigc::track_obj([this](
                                const Glib::RefPtr<Gst::Bus>&,
                                const Glib::RefPtr<Gst::Message>& message) {
        utils::debug::scope_log log(DBG_LVL_2("gtox"), {});
        Glib::RefPtr<Gst::MessageError> error;

        switch (message->get_message_type()) {
            case Gst::MESSAGE_EOS:
                property_state() = Gst::STATE_NULL;
                break;
            case Gst::MESSAGE_ERROR:
                error = decltype(error)::cast_static(message);
                if (error) {
                    m_signal_error(error->parse().what());
                } else {
                    m_signal_error(_("unknown gstreamer error"));
                }
                break;
            default:
                break;
        }

        return true;
    }, *this));

    m_playbin->property_uri().set_value(property_uri());

    m_volume  = Glib::Binding::bind_property(
                        property_volume(),
                        m_playbin->property_volume(),
                        Glib::BINDING_DEFAULT
                        | Glib::BINDING_SYNC_CREATE);

    property_state().signal_changed().connect(sigc::track_obj([this]() {
        utils::debug::scope_log log(DBG_LVL_2("gtox"), {});
        if (m_playbin) {
            m_playbin->set_state(property_state());
        }
    }, *this));

    m_playbin->set_state(property_state());
}

void gstreamer::destroy() {
    if (m_playbin) {
        m_playbin->set_state(Gst::STATE_NULL);
    }
    m_appsink = Glib::RefPtr<Gst::AppSink>();
    m_playbin = Glib::RefPtr<Gst::PlayBin>();
}

gstreamer::gstreamer():
    Glib::ObjectBase(typeid(gstreamer)),
    m_property_uri(*this, "gstreamer-uri"),
    m_property_state(*this, "gstreamer-state", Gst::STATE_NULL),
    m_property_position(*this, "gstreamer-position", 0),
    m_property_duration(*this, "gstreamer-duration", 0),
    m_property_volume(*this, "gstreamer-volume", 0.5),
    m_property_pixbuf(*this, "gstreamer-pixbuf") {

    property_state().signal_changed().connect(sigc::track_obj([this]() {
       if (!m_playbin && property_state() != Gst::STATE_NULL) {
           init();
       } else if (property_state() == Gst::STATE_NULL) {
           destroy();
           /*m_property_position.set_value(0);
           m_property_duration.set_value(0);*/
       }
    }, *this));

    property_uri().signal_changed().connect(sigc::track_obj([this]() {
        // uri changed
        destroy();
        m_property_position.set_value(0);
        m_property_duration.set_value(0);
    }, *this));
}

gstreamer::~gstreamer() {
    utils::debug::scope_log log(DBG_LVL_1("gtox"), {});
    destroy();
}

void gstreamer::set_position(gint64 pos) {
    if (!m_playbin) {
        return;
    }
    m_property_position = pos;
    m_playbin->seek(
                Gst::FORMAT_TIME,
                Gst::SEEK_FLAG_FLUSH | Gst::SEEK_FLAG_KEY_UNIT,
                pos);
}

std::pair<bool, bool> gstreamer::has_video_audio(Glib::ustring uri) {
    utils::debug::scope_log log(DBG_LVL_1("gtox"), { uri.raw() });
    //http://gstreamer.freedesktop.org/data/doc/gstreamer/head/manual/html/chapter-metadata.html
    auto pipeline = Gst::Pipeline::create();
    auto decoder  = Gst::UriDecodeBin::create();
    auto sink     = Gst::ElementFactory::create_element("fakesink");

    pipeline->add(decoder);
    pipeline->add(sink);

    decoder->property_uri() = uri;

    decoder->signal_pad_added().connect([sink](const Glib::RefPtr<Gst::Pad>& pad) {
        utils::debug::scope_log log(DBG_LVL_2("gtox"), {});
        //Why am I doing this here ?
        auto sinkpad = sink->get_static_pad("sink");
        if (!sinkpad->is_linked()) {
            if (pad->link(sinkpad) != Gst::PAD_LINK_OK) {
                g_error("Failed to link pads !");
            }
        }
    });

    pipeline->set_state(Gst::STATE_PAUSED);

    bool found_video_stream  = false;
    bool found_audio_stream  = false;
    while (true) {
        auto msg = pipeline->get_bus()->pop(Gst::CLOCK_TIME_NONE, Gst::MESSAGE_ASYNC_DONE | Gst::MESSAGE_TAG | Gst::MESSAGE_ERROR);

        if (msg->get_message_type() != Gst::MESSAGE_TAG) {
            break;
        }

        auto tag = Glib::RefPtr<Gst::MessageTag>::cast_static(msg);
        auto tag_list = tag->parse();
        tag_list.foreach([&](const Glib::ustring& tag){
            if (tag == "video-codec") {
                found_video_stream = true;
            } else if (tag == "audio-codec") {
                found_audio_stream = true;
            }
        });
    }

    pipeline->set_state(Gst::STATE_NULL);

    return {found_video_stream, found_audio_stream};
}

Glib::RefPtr<Gdk::Pixbuf> gstreamer::get_video_preview(Glib::ustring uri) {
    utils::debug::scope_log log(DBG_LVL_1("gtox"), { uri.raw() });

    bool has_video, has_audio;
    std::tie(has_video, has_audio) = gstreamer::has_video_audio(uri);
    if (!has_video) {
        return Glib::RefPtr<Gdk::Pixbuf>();
    }

    gstreamer video;
    video.property_uri() = uri;

    bool seeked = false;
    video.property_duration().signal_changed().connect([&]() {
        if (seeked) {
            return;
        }
        // seek to 1/3 of the video
        video.set_position(video.property_duration().get_value() / 3);
        seeked = true;
    });

    std::promise<Glib::RefPtr<Gdk::Pixbuf>> pix_promise;
    bool ignore_first = true;
    video.property_pixbuf().signal_changed().connect([&]() {
        if (ignore_first) {
            ignore_first = false;
            return;
        }
        // set the preview image
        pix_promise.set_value(video.property_pixbuf());
    });

    video.signal_error().connect([&](Glib::ustring) {
        // set empty preview image
        pix_promise.set_value(Glib::RefPtr<Gdk::Pixbuf>());
    });

    video.property_state() = Gst::STATE_PAUSED;

    // the future will wait until the preview image exists !
    return pix_promise.get_future().get();
}

Glib::RefPtr<Gdk::Pixbuf> gstreamer::extract_frame(Glib::RefPtr<Gst::Sample> sample,
                                                   std::shared_ptr<std::pair<int, int>> resolution) {
    utils::debug::scope_log log(DBG_LVL_5("gtox"), {});
    static auto null = Glib::RefPtr<Gdk::Pixbuf>();
    if (!sample) {
        return null;
    }
    /*
    Generates crashes, not idea why
    using "resolution" now..
    gets filled by preroll

    auto caps = sample->get_caps();
    if (!caps) {
        return null;
    }
    if (caps->empty()) {
        return null;
    }
    if (caps->empty()) {
        return null;
    }
    auto struc = caps->get_structure(0);
    int w,h;
    if (!struc.get_field("width", w)) {
        return null;
    }
    if (!struc.get_field("height", h)) {
        return null;
    }
    */
    auto buffer = sample->get_buffer();
    if (!buffer) {
        return null;
    }
    auto map = Glib::RefPtr<Gst::MapInfo>(new Gst::MapInfo);
    if (!buffer->map(map, Gst::MAP_READ)) {
        return null;
    }
    auto mem = new uint8_t[map->get_size()];
    std::copy(map->get_data(), map->get_data() + map->get_size(), mem);
    buffer->unmap(map);

    return Gdk::Pixbuf::create_from_data(mem,
                                               Gdk::COLORSPACE_RGB,
                                               false,
                                               8,
                                               resolution->first,
                                               resolution->second,
                                               GST_ROUND_UP_4( resolution->first*3 ),
                                               [](const guint8* data) {
        delete[] data;
    });
}
