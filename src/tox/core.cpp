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
#include "core.h"
#include "contact/manager.h"
#include "exception.h"
#include <iostream>

using namespace toxmm;

//SIGNALS
core::type_signal_contact_request           core::signal_contact_request() { return m_signal_contact_request; }
core::type_signal_contact_message           core::signal_contact_message() { return m_signal_contact_message; }
core::type_signal_contact_action            core::signal_contact_action()  { return m_signal_contact_action; }
core::type_signal_contact_name              core::signal_contact_name()    { return m_signal_contact_name; }
core::type_signal_contact_status_message    core::signal_contact_status_message()  { return m_signal_contact_status_message; }
core::type_signal_contact_status            core::signal_contact_status()  { return m_signal_contact_status; }
core::type_signal_contact_typing            core::signal_contact_typing()  { return m_signal_contact_typing; }
core::type_signal_contact_read_receipt      core::signal_contact_read_receipt()  { return m_signal_contact_read_receipt; }
core::type_signal_contact_connection_status core::signal_contact_connection_status()  { return m_signal_contact_connection_status; }
core::type_signal_file_chunk_request        core::signal_file_chunk_request()  { return m_signal_file_chunk_request; }
core::type_signal_file_recv                 core::signal_file_recv()  { return m_signal_file_recv; }
core::type_signal_file_recv_chunk           core::signal_file_recv_chunk()  { return m_signal_file_recv_chunk; }
core::type_signal_file_recv_control         core::signal_file_recv_control() { return m_signal_file_recv_control; }

Glib::PropertyProxy_ReadOnly<contactAddr> core::property_addr() {
    return {this, "self-addr"};
}
Glib::PropertyProxy_ReadOnly<contactAddrPublic> core::property_addr_public() {
    return {this, "self-addr-public"};
}
Glib::PropertyProxy<Glib::ustring> core::property_name() {
    return m_property_name.get_proxy();
}
Glib::PropertyProxy_ReadOnly<Glib::ustring> core::property_name_or_addr() {
    return {this, "self-name-or-addr"};
}
Glib::PropertyProxy<Glib::ustring> core::property_status_message() {
    return m_property_status_message.get_proxy();
}
Glib::PropertyProxy<TOX_USER_STATUS> core::property_status() {
    return m_property_status.get_proxy();
}
Glib::PropertyProxy_ReadOnly<TOX_CONNECTION> core::property_connection() {
    return {this, "self-connection"};
}
Glib::PropertyProxy<Glib::ustring> core::property_download_path() {
    return m_property_download_path.get_proxy();
}
Glib::PropertyProxy<Glib::ustring> core::property_avatar_path() {
    return m_property_avatar_path.get_proxy();
}

core::core(const std::string& profile_path, const std::shared_ptr<toxmm::storage>& storage):
    Glib::ObjectBase(typeid(core)),
    m_profile_path(profile_path),
    m_storage(storage),
    m_property_addr(*this, "self-addr"),
    m_property_addr_public(*this, "self-addr-public"),
    m_property_name(*this, "self-name"),
    m_property_name_or_addr(*this, "self-name-or-addr"),
    m_property_status_message(*this, "self-status-message"),
    m_property_status(*this, "self-status"),
    m_property_connection(*this, "self-connection"),
    m_property_download_path(*this, "core-download-path",
                             Glib::get_user_special_dir(
                                 GUserDirectory::G_USER_DIRECTORY_DOWNLOAD)),
    m_property_avatar_path(*this, "core-avatar-path",
                           Glib::build_filename(
                               Glib::get_user_config_dir(),
                               "tox",
                               "avatars")) {
    //rest is done in init()
}

void core::save() {
    //TODO: save

}

void core::destroy() {
    m_contact_manager->destroy();
    m_contact_manager.reset();
}

core::~core() {
    tox_kill(m_toxcore);
}

std::shared_ptr<core> core::create(const std::string& profile_path,
                                   const std::shared_ptr<toxmm::storage>& storage) {
    auto tmp = std::shared_ptr<core>(new core(profile_path, storage));
    tmp->init();
    return tmp;
}

std::vector<uint8_t> core::create_state(std::string name, std::string status, contactAddrPublic& out_addr_public) {
    TOX_ERR_OPTIONS_NEW nerror;
    auto options = std::shared_ptr<Tox_Options>(tox_options_new(&nerror),
                                                [](Tox_Options* p) {
                                                    tox_options_free(p);
                                                });
    if (nerror != TOX_ERR_OPTIONS_NEW_OK) {
        throw exception(nerror);
    }
    options->savedata_type   = TOX_SAVEDATA_TYPE_NONE;
    TOX_ERR_NEW error;
    Tox* m_toxcore = tox_new(options.get(), &error);
    if (error != TOX_ERR_NEW_OK) {
        throw exception(error);
    }
    //set name
    TOX_ERR_SET_INFO serror;
    tox_self_set_name(m_toxcore, (const uint8_t*)name.data(), name.size(), &serror);
    if (serror != TOX_ERR_SET_INFO_OK) {
        throw exception(error);
    }
    //set status
    tox_self_set_status_message(m_toxcore, (const uint8_t*)status.data(), status.size(), &serror);
    if (serror != TOX_ERR_SET_INFO_OK) {
        throw exception(error);
    }
    //get addr
    tox_self_get_public_key(m_toxcore, out_addr_public);
    //get state
    std::vector<uint8_t> state(tox_get_savedata_size(m_toxcore));
    tox_get_savedata(m_toxcore, state.data());
    return state;
}

void core::try_load(std::string path, Glib::ustring& out_name, Glib::ustring& out_status, contactAddrPublic& out_addr_public, bool& out_writeable) {
    profile m_profile;
    m_profile.open(path);
    if (!m_profile.can_read()) {
        throw std::runtime_error("Couldn't read toxcore profile");
    }
    TOX_ERR_OPTIONS_NEW nerror;
    auto options = std::shared_ptr<Tox_Options>(tox_options_new(&nerror),
                                                [](Tox_Options* p) {
                                                    tox_options_free(p);
                                                });
    if (nerror != TOX_ERR_OPTIONS_NEW_OK) {
        throw exception(nerror);
    }
    auto state = m_profile.read();
    if (state.empty()) {
        throw std::runtime_error("Empty state ?");
    }
    options->savedata_type   = TOX_SAVEDATA_TYPE_TOX_SAVE;
    options->savedata_data   = state.data();
    options->savedata_length = state.size();
    TOX_ERR_NEW error;
    Tox* m_toxcore = tox_new(options.get(), &error);
    if (error != TOX_ERR_NEW_OK) {
        throw exception(error);
    }
    //get name
    auto size = tox_self_get_name_size(m_toxcore);
    out_name.resize(size);
    tox_self_get_name(m_toxcore, (uint8_t*)out_name.raw().data());
    out_name = fix_utf8(out_name);
    //get status
    size = tox_self_get_status_message_size(m_toxcore);
    out_status.resize(size);
    tox_self_get_status_message(m_toxcore, (uint8_t*)out_status.raw().data());
    out_status = fix_utf8(out_status);
    //get addr
    tox_self_get_public_key(m_toxcore, out_addr_public);
    //check writeable
    out_writeable = m_profile.can_write();
}

void core::init() {
    m_profile.open(m_profile_path);
    if (!m_profile.can_read()) {
        throw std::runtime_error("Couldn't read toxcore profile");
    }

    TOX_ERR_OPTIONS_NEW nerror;
    auto options = std::shared_ptr<Tox_Options>(tox_options_new(&nerror),
                                                [](Tox_Options* p) {
                                                    tox_options_free(p);
                                                });
    if (nerror != TOX_ERR_OPTIONS_NEW_OK) {
        throw exception(nerror);
    }

    options->ipv6_enabled = true;
    options->udp_enabled  = true;

    std::vector<uint8_t> state = m_profile.read();
    if (state.empty()) {
        options->savedata_type   = TOX_SAVEDATA_TYPE_NONE;
    } else {
        options->savedata_type   = TOX_SAVEDATA_TYPE_TOX_SAVE;
        options->savedata_data   = state.data();
        options->savedata_length = state.size();
    }
    TOX_ERR_NEW error;
    m_toxcore = tox_new(options.get(), &error);

    if (error != TOX_ERR_NEW_OK) {
        throw exception(error);
    }

    auto pub = from_hex(
                   "951C88B7E75C8674"
                   "18ACDB5D27382137"
                   "2BB5BD652740BCDF"
                   "623A4FA293E75D2F");
    auto host = "192.254.75.102";
    TOX_ERR_BOOTSTRAP berror;
    if (!tox_bootstrap(m_toxcore, host, 33445, pub.data(), &berror)) {
        throw exception(berror);
    }

    //install events:
    tox_callback_friend_request(toxcore(), [](Tox*, const uint8_t* addr, const uint8_t* data, size_t len, void* _this) {
        ((core*)_this)->m_signal_contact_request(contactAddr(addr), core::fix_utf8(data, len));
    }, this);
    tox_callback_friend_message(toxcore(), [](Tox*, uint32_t nr, TOX_MESSAGE_TYPE type, const uint8_t* message, size_t len, void* _this) {
        if (type == TOX_MESSAGE_TYPE_NORMAL) {
            ((core*)_this)->m_signal_contact_message(contactNr(nr), core::fix_utf8(message, len));
        } else {
            ((core*)_this)->m_signal_contact_action(contactNr(nr), core::fix_utf8(message, len));
        }
    }, this);
    tox_callback_friend_name(toxcore(), [](Tox*, uint32_t nr, const uint8_t* name, size_t len, void* _this) {
        ((core*)_this)->m_signal_contact_name(contactNr(nr), core::fix_utf8(name, len));
    }, this);
    tox_callback_friend_status_message(toxcore(), [](Tox*, uint32_t nr, const uint8_t* status_message, size_t len, void* _this) {
        ((core*)_this)->m_signal_contact_status_message(contactNr(nr), core::fix_utf8(status_message, len));
    }, this);
    tox_callback_friend_status(toxcore(), [](Tox*, uint32_t nr, TOX_USER_STATUS status, void* _this) {
        ((core*)_this)->m_signal_contact_status(contactNr(nr), status);
    }, this);
    tox_callback_friend_typing(toxcore(), [](Tox*, uint32_t nr, bool is_typing, void* _this) {
        ((core*)_this)->m_signal_contact_typing(contactNr(nr), is_typing);
    }, this);
    tox_callback_friend_read_receipt(toxcore(), [](Tox*, uint32_t nr, uint32_t receipt, void* _this) {
        ((core*)_this)->m_signal_contact_read_receipt(contactNr(nr), receiptNr(receipt));
    }, this);
    tox_callback_friend_connection_status(toxcore(), [](Tox*, uint32_t nr, TOX_CONNECTION status, void* _this) {
        ((core*)_this)->m_signal_contact_connection_status(contactNr(nr), status);
    }, this);
    tox_callback_self_connection_status(toxcore(), [](Tox *, TOX_CONNECTION connection_status, void* _this) {
        ((core*)_this)->m_property_connection = connection_status;
    }, this);
    tox_callback_file_chunk_request(toxcore(), [](Tox*, uint32_t nr, uint32_t file_number, uint64_t position, size_t length, void *_this) {
        ((core*)_this)->m_signal_file_chunk_request(contactNr(nr), fileNr(file_number), position, length);
    }, this);
    tox_callback_file_recv(toxcore(), [](Tox*, uint32_t nr, uint32_t file_number, uint32_t kind, uint64_t file_size, const uint8_t *filename, size_t filename_length, void *_this) {
        ((core*)_this)->m_signal_file_recv(contactNr(nr), fileNr(file_number), TOX_FILE_KIND(kind), file_size, core::fix_utf8(filename, filename_length));
    }, this);
    tox_callback_file_recv_chunk(toxcore(), [](Tox*, uint32_t nr, uint32_t file_number, uint64_t position, const uint8_t *data, size_t length, void *_this) {
        ((core*)_this)->m_signal_file_recv_chunk(contactNr(nr), fileNr(file_number), position, std::vector<uint8_t>(data, data+length));
    }, this);
    tox_callback_file_recv_control(toxcore(), [](Tox*, uint32_t nr, uint32_t file_number, TOX_FILE_CONTROL state, void* _this) {
       ((core*)_this)->m_signal_file_recv_control(contactNr(nr), fileNr(file_number), state);
    }, this);

    //install logic for name_or_addr
    auto update_name_or_addr = [this]() {
        if (property_name().get_value().empty()) {
            m_property_name_or_addr = Glib::ustring(m_property_addr.get_value());
        } else {
            m_property_name_or_addr = property_name().get_value();
        }
    };
    property_name().signal_changed().connect(sigc::track_obj(update_name_or_addr, *this));
    property_addr().signal_changed().connect(sigc::track_obj(update_name_or_addr, *this));

    //get addr
    contactAddr addr;
    tox_self_get_address(m_toxcore, addr);
    m_property_addr = addr;
    contactAddrPublic addr_public;
    tox_self_get_public_key(m_toxcore, addr_public);
    m_property_addr_public = addr_public;
    //get name
    std::string name(tox_self_get_name_size(m_toxcore), 0);
    tox_self_get_name(m_toxcore, (uint8_t*)name.data());
    m_property_name = fix_utf8(name);
    //get status_message
    std::string status_message(tox_self_get_status_message_size(m_toxcore), 0);
    tox_self_get_status_message(m_toxcore, (uint8_t*)status_message.data());
    m_property_status_message = fix_utf8(status_message);
    //get status
    m_property_status = tox_self_get_status(m_toxcore);
    //get connection
    m_property_connection = tox_self_get_connection_status(m_toxcore);

    //install change events for properties
    property_name().signal_changed().connect(sigc::track_obj([this]() {
        auto name = property_name().get_value();
        TOX_ERR_SET_INFO error;
        tox_self_set_name(m_toxcore, (const uint8_t*)name.data(), name.size(), &error);
        if (error != TOX_ERR_SET_INFO_OK) {
            throw exception(error);
        }
    }, *this));
    property_status_message().signal_changed().connect(sigc::track_obj([this]() {
        auto status_message = property_status_message().get_value();
        TOX_ERR_SET_INFO error;
        tox_self_set_status_message(m_toxcore, (const uint8_t*)status_message.data(), status_message.size(), &error);
        if (error != TOX_ERR_SET_INFO_OK) {
            throw exception(error);
        }
    }, *this));
    property_status().signal_changed().connect(sigc::track_obj([this]() {
        tox_self_set_status(m_toxcore, property_status());
    }, *this));

    //set prefix for storage
    m_storage->set_prefix_key(property_addr_public().get_value());

    //start sub systems:
    m_contact_manager = std::shared_ptr<toxmm::contact_manager>(new toxmm::contact_manager(shared_from_this()));
    m_contact_manager->init();
}

std::shared_ptr<toxmm::contact_manager> core::contact_manager() {
    return m_contact_manager;
}

std::shared_ptr<toxmm::storage> core::storage() {
    return m_storage;
}

void core::update() {
    tox_iterate(toxcore());
}

uint32_t core::update_optimal_interval() {
    return tox_iteration_interval(m_toxcore);
}

Glib::ustring core::fix_utf8(const std::string& input) {
    return fix_utf8((const uint8_t*)input.data(), input.size());
}

Glib::ustring core::fix_utf8(const uint8_t* input, int size) {
    return fix_utf8((const int8_t*)input, size);
}

Glib::ustring core::fix_utf8(const int8_t* input, int size) {
    static const Glib::ustring uFFFD(1, gunichar(0xFFFD));
    std::string fixed;
    fixed.reserve(size);
    const gchar* ginput = (gchar*)input;
    const gchar* str = ginput;
    const gchar* last_valid;
    while(!g_utf8_validate(str, std::distance(str, ginput + size), &last_valid)) {
        fixed.append(str, last_valid);
        fixed.append(uFFFD.raw().begin(), uFFFD.raw().end());
        str = last_valid + 1;
    }
    fixed.append(str, last_valid);
    return fixed;
}

Glib::ustring core::to_hex(const uint8_t* data, size_t len) {
    std::string s;
    for (size_t i = 0; i < len; ++i) {
        static const int8_t hex[] = "0123456789ABCDEF";
        s += hex[(data[i] >> 4) & 0xF];
        s += hex[data[i] & 0xF];
    }
    return s;
}

std::vector<uint8_t> core::from_hex(std::string data) {
    std::vector<uint8_t> tmp;
    tmp.reserve(tmp.size() / 2);
    for (size_t i = 0; i < data.size(); i += 2) {
        tmp.push_back(std::stoi(data.substr(i, 2), 0, 16));  // pretty stupid
    }
    return tmp;
}

Tox* core::toxcore() {
    return m_toxcore;
}

toxmm::hash core::hash(const std::vector<uint8_t>& data) {
    toxmm::hash res;
    tox_hash(res, data.data(), data.size());
    return res;
}
