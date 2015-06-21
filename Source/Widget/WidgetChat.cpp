/**
    gTox a GTK-based tox-client - https://github.com/KoKuToru/gTox.git

    Copyright (C) 2014  Luca Béla Palkovics
    Copyright (C) 2014  Maurice Mohlek

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
#include "WidgetChat.h"
#include "Tox/Toxmm.h"
#include "WidgetChatLine.h"
#include "Chat/WidgetChatLabel.h"
#include <glibmm/i18n.h>
#include <iostream>

namespace sigc {
    SIGC_FUNCTORS_DEDUCE_RESULT_TYPE_WITH_DECLTYPE
}

WidgetChat::WidgetChat(gToxObservable* instance, Toxmm::FriendNr nr)
    : Glib::ObjectBase("WidgetChat"), m_nr(nr), m_autoscroll(true) {

    set_observable(instance);

    m_output.set_editable(false);
    m_scrolled.add(m_vbox);
    m_vbox.set_spacing(5);
    auto frame = Gtk::manage(new Gtk::Frame());
    frame->add(m_scrolled);
    pack1(*frame, true, false);
    frame->get_style_context()->remove_class("frame");
    // pack2(input, false, true);

    m_btn_send.set_label(_("SEND"));

    m_hbox.pack_start(m_input, true, true);
    m_hbox.pack_end(m_btn_send, false, false);
    pack2(m_hbox, false, false);

    m_input.set_wrap_mode(Gtk::WRAP_WORD_CHAR);

    // set_position(400);
    m_hbox.set_size_request(-1, 80);

    m_input.signal_key_press_event().connect(
        [this](GdkEventKey* event) {
            auto text_buffer = m_input.get_buffer();
            if (event->keyval == GDK_KEY_Return
                && !(event->state & GDK_SHIFT_MASK)) {
                if (text_buffer->begin() != text_buffer->end()) {
                    m_btn_send.clicked();
                    return true;
                }
            }

            return false;
        },
        false);

    m_btn_send.signal_clicked().connect([this]() {
        try {
            bool allow_send = m_input.get_buffer()->get_text().find_first_not_of(" \t\n\v\f\r") != std::string::npos;

            if(!allow_send)
                return;

            // add to chat
            auto text = m_input.get_serialized_text();
            add_line(false, WidgetChatLine::Line{
                         false,
                         true,
                         tox().send_message(get_friend_nr(), text),
                         0,
                         get_friend_nr(),
                         text
                     });

            // clear chat input
            m_input.get_buffer()->set_text("");
        } catch (...) {
            // not online ?
        }
    });

    m_vbox.set_name("WidgetChat");
    m_vbox.property_margin() = 10;  // wont work via css

    m_scrolled.get_vadjustment()->signal_value_changed().connect_notify(
        [this]() {
            // check if lowest position
            auto adj = m_scrolled.get_vadjustment();
            m_autoscroll = adj->get_upper() - adj->get_page_size()
                           == adj->get_value();
        });

    m_vbox.signal_size_allocate().connect_notify([this](Gtk::Allocation&) {
        // auto scroll:
        if (m_autoscroll) {
            auto adj = m_scrolled.get_vadjustment();
            adj->set_value(adj->get_upper() - adj->get_page_size());
        }
    });

    // Disable scroll to focused child
    auto viewport = dynamic_cast<Gtk::Viewport*>(m_scrolled.get_child());
    if (viewport) {
        auto dummy_adj = Gtk::Adjustment::create(0, 0, 0);
        viewport->set_focus_hadjustment(dummy_adj);
        viewport->set_focus_vadjustment(dummy_adj);
    }

    // load log
    auto log = tox().get_log(nr);
    for (auto l : log) {
        if (l.sendtime != 0) {
            add_line(false, WidgetChatLine::Line{
                         false,
                         false,
                         0,
                         l.sendtime,
                         nr,
                         l.data
                     });
        } else {
            add_line(true, WidgetChatLine::Line{
                         false,
                         false,
                         0,
                         l.recvtime,
                         0,
                         l.data
                     });
        }
    }

    m_tox_callback = observer_add([this, nr](const ToxEvent& ev) {
        if (ev.type() == typeid(Toxmm::EventFriendAction)) {
            auto data = ev.get<Toxmm::EventFriendAction>();
            if (nr == data.nr) {
                add_line(true, WidgetChatLine::Line{
                             false,
                             false,
                             0,
                             0,
                             data.nr,
                             data.message
                         });
            }
        } else if (ev.type() == typeid(Toxmm::EventFriendMessage)) {
            auto data = ev.get<Toxmm::EventFriendMessage>();
            if (nr == data.nr) {
                add_line(true, WidgetChatLine::Line{
                             false,
                             false,
                             0,
                             0,
                             data.nr,
                             data.message
                         });
            }
        }
    });
}

WidgetChat::~WidgetChat() {
}

void WidgetChat::focus() {
    m_input.grab_focus();
}

Toxmm::FriendNr WidgetChat::get_friend_nr() const {
    return m_nr;
}

void WidgetChat::add_line(Glib::ustring text) {
    //what does this do ?
    m_output.add_line(text);
}

void WidgetChat::add_line(bool left_side, WidgetChatLine::Line new_line) {
    // check if time i set, if not we will give it actual time
    if (new_line.timestamp == 0) {
        new_line.timestamp = Glib::DateTime::create_now_utc().to_unix();
    }
    auto new_time = Glib::DateTime::create_now_utc(new_line.timestamp);
    new_time = Glib::DateTime::create_utc(new_time.get_year(),
                                          new_time.get_month(),
                                          new_time.get_day_of_month(),
                                          0,
                                          0,
                                          0);
    decltype(new_time) last_time = Glib::DateTime::create_now_utc(0);

    bool action = new_line.message.find("/me ") == 0;

    // check last message blob
    std::vector<Gtk::Widget*> childs = m_vbox.get_children();
    if (!childs.empty()) {
        WidgetChatLine* item = dynamic_cast<WidgetChatLine*>(childs.back());
        if (item != nullptr) {
            last_time = Glib::DateTime::create_now_utc(item->last_timestamp());
            last_time = Glib::DateTime::create_utc(last_time.get_year(),
                                                   last_time.get_month(),
                                                   last_time.get_day_of_month(),
                                                   0,
                                                   0,
                                                   0);
            // check if blob is on same side
            if (!action && item->get_side() == left_side) {
                // check if it's same day month year
                if (last_time.compare(new_time) == 0) {
                    item->add_line(new_line);
                    return;
                }
            }
        }
    }

    // check if we need to add a date-line
    if (last_time.compare(new_time) != 0) {
        // add a date message
        auto msg = Gtk::manage(new WidgetChatLabel() /*new Gtk::Label()*/);
        msg->set_text(Glib::DateTime::create_now_local(new_line.timestamp)
                          .format(_("DATE_FORMAT")));
        msg->set_name("ChatTime");
        msg->set_halign(Gtk::ALIGN_CENTER);
        msg->show_all();
        m_vbox.pack_start(*msg, false, false);
    }

    if (!action) {
        // add new line
        auto new_bubble = Gtk::manage(new WidgetChatLine(observable(), left_side?m_nr:~0u, left_side));
        new_bubble->add_line(new_line);
        new_bubble->show_all();
        m_vbox.pack_start(*new_bubble, false, false);
    } else {
        // TODO add a WidgetChatLineMe ..
        // add new action line
        auto msg = Gtk::manage(new WidgetChatLabel() /*new Gtk::Label()*/);
        auto name = left_side ? tox().get_name_or_address(m_nr)
                              : tox().get_name_or_address();
        msg->set_text(name + new_line.message.substr(Glib::ustring("/me").size()));
        msg->set_name("ChatTime");
        msg->set_halign(Gtk::ALIGN_CENTER);
        msg->show_all();
        m_vbox.pack_start(*msg, false, false);
    }
}
