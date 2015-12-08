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
#include "videoplayer.h"
#include <iomanip>

#ifndef SIGC_CPP11_HACK
#define SIGC_CPP11_HACK
namespace sigc {
    SIGC_FUNCTORS_DEDUCE_RESULT_TYPE_WITH_DECLTYPE
}
#endif
using namespace widget;

Glib::PropertyProxy<Glib::RefPtr<Gdk::Pixbuf>> videoplayer::property_preview_pixbuf() {
    return m_property_helper.m_property_preview_pixbuf.get_proxy();
}

videoplayer::videoplayer(BaseObjectType* cobject,
                         utils::builder builder):
    Glib::ObjectBase(typeid(videoplayer)),
    Gtk::Box(cobject) {
    utils::debug::scope_log log(DBG_LVL_1("gtox"), {});

    builder.get_widget("video_control", m_eventbox);
    builder.get_widget("video_control_box", m_control);
    builder.get_widget("video_seek", m_seek);
    builder.get_widget("video_play", m_play_btn);
    builder.get_widget("video_pause", m_pause_btn);
    builder.get_widget("video_stop", m_stop_btn);
    builder.get_widget("video_position", m_position);
    builder.get_widget("video_duration", m_duration);
    builder.get_widget("video_volume", m_volume);

    m_video = builder.get_widget_derived<widget::imagescaled>("video");

    auto flags = Glib::BINDING_DEFAULT | Glib::BINDING_SYNC_CREATE;
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_streamer.property_volume(),
                             m_volume->get_adjustment()->property_value(),
                             flags | Glib::BINDING_BIDIRECTIONAL));
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_streamer.property_position(),
                             m_seek->get_adjustment()->property_value(),
                             flags));
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_streamer.property_duration(),
                             m_seek->get_adjustment()->property_upper(),
                             flags));
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_streamer.property_position(),
                             m_position->property_label(),
                             flags,
                             [](const gint64& input, Glib::ustring& output) {
        utils::debug::scope_log log(DBG_LVL_4("gtox"), { (long long)(input) });
        output = Glib::ustring::compose(
                    "%1:%2:%3",
                    Glib::ustring::format(std::setw(3), std::setfill(L'0'), std::right, Gst::get_hours(input)),
                    Glib::ustring::format(std::setw(3), std::setfill(L'0'), std::right, Gst::get_minutes(input)),
                    Glib::ustring::format(std::setw(3), std::setfill(L'0'), std::right, Gst::get_seconds(input)));
        return true;
    }));
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_streamer.property_duration(),
                             m_duration->property_label(),
                             flags,
                             [](const gint64& input, Glib::ustring& output) {
        utils::debug::scope_log log(DBG_LVL_4("gtox"), { (long long)(input) });
        output = Glib::ustring::compose(
                    "%1:%2:%3",
                    Glib::ustring::format(std::setw(3), std::setfill(L'0'), std::right, Gst::get_hours(input)),
                    Glib::ustring::format(std::setw(3), std::setfill(L'0'), std::right, Gst::get_minutes(input)),
                    Glib::ustring::format(std::setw(3), std::setfill(L'0'), std::right, Gst::get_seconds(input)));
        return true;
    }));

    m_control->get_style_context()->add_class("gtox-opacity-0");
    m_eventbox->add_events(Gdk::ENTER_NOTIFY_MASK);
    m_eventbox->signal_enter_notify_event()
            .connect(sigc::track_obj([this](GdkEventCrossing*) {
        m_control->get_style_context()->remove_class("gtox-opacity-0");
        //start time to detect leave
        m_leave_timer.disconnect();
        m_leave_timer = Glib::signal_timeout().connect_seconds(sigc::track_obj([this]() {
            //check if mouse cursor is still in area of the eventbox
            int x, y;
            m_eventbox->get_pointer(x, y);

            if (x < 0 ||
                y < 0 ||
                x > m_eventbox->get_allocated_width() ||
                y > m_eventbox->get_allocated_height()) {
                //we are outside !
                m_control->get_style_context()->add_class("gtox-opacity-0");
                //stop the timer
                return false;
            }
            return true;
        }, *this), 5);
        return false;
    }, *this));

    //Button handling
    auto property_state_update = [this]() {
        utils::debug::scope_log log(DBG_LVL_4("gtox"), {});
        auto state = m_streamer.property_state().get_value();
        bool playing = state == Gst::STATE_PLAYING;
        bool paused  = state == Gst::STATE_PAUSED;
        bool stopped = state == Gst::STATE_NULL;

        if (playing != m_play_btn->property_active()) {
            m_play_btn->property_active() = playing;
        }
        if (paused != m_pause_btn->property_active()) {
            m_pause_btn->property_active() = paused;
        }
        if (stopped != m_stop_btn->property_active()) {
            m_stop_btn->property_active() = stopped;
        }

        if (stopped) {
            m_video->property_pixbuf() = property_preview_pixbuf().get_value();
        }
    };
    m_streamer.property_state()
            .signal_changed()
            .connect(sigc::track_obj(property_state_update, *this));
    property_state_update();

    m_bindings.push_back(Glib::Binding::bind_property(
                             m_play_btn->property_active(),
                             m_play_btn->property_sensitive(),
                             flags | Glib::BINDING_INVERT_BOOLEAN));
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_pause_btn->property_active(),
                             m_pause_btn->property_sensitive(),
                             flags | Glib::BINDING_INVERT_BOOLEAN));
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_stop_btn->property_active(),
                             m_stop_btn->property_sensitive(),
                             flags | Glib::BINDING_INVERT_BOOLEAN));

    m_play_btn->signal_clicked().connect(sigc::track_obj([this]() {
        utils::debug::scope_log log(DBG_LVL_2("gtox"), {});
        if (m_play_btn->property_active()) {
           m_streamer.property_state() = Gst::STATE_PLAYING;
        }
    }, *this));
    m_pause_btn->signal_clicked().connect(sigc::track_obj([this]() {
        utils::debug::scope_log log(DBG_LVL_2("gtox"), {});
        if (m_pause_btn->property_active()) {
            m_streamer.property_state() = Gst::STATE_PAUSED;
        }
    }, *this));
    m_stop_btn->signal_clicked().connect(sigc::track_obj([this]() {
        utils::debug::scope_log log(DBG_LVL_2("gtox"), {});
        if (m_stop_btn->property_active()) {
            m_streamer.property_state() = Gst::STATE_NULL;
        }
    }, *this));

    m_streamer.property_uri().signal_changed().connect(sigc::track_obj([this]() {
        utils::debug::scope_log log(DBG_LVL_2("gtox"), {});
        m_streamer.property_state() = Gst::STATE_NULL;
    }, *this));

    //display frame
    m_bindings.push_back(Glib::Binding::bind_property(
                             m_streamer.property_pixbuf(),
                             m_video->property_pixbuf(),
                             flags));

    property_preview_pixbuf().signal_changed().connect(sigc::track_obj([this]() {
        m_video->property_pixbuf() = property_preview_pixbuf().get_value();
    }, *this));
}

videoplayer::~videoplayer() {
    utils::debug::scope_log log(DBG_LVL_1("gtox"), {});
}

utils::builder::ref<videoplayer> videoplayer::create() {
    utils::debug::scope_log log(DBG_LVL_1("gtox"), {});
    return utils::builder::create_ref<videoplayer>(
                "/org/gtox/ui/videoplayer.ui",
                "videoplayer");
}
