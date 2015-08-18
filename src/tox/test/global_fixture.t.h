#ifndef GLOBAL_FIXTURE
#define GLOBAL_FIXTURE

#include <cxxtest/TestSuite.h>
#include <cxxtest/GlobalFixture.h>
#include "../types.h"
#include "../core.h"
#include "../storage.h"
#include "../contact/manager.h"
#include "../contact/contact.h"
#include <chrono>

class GlobalFixture : public CxxTest::GlobalFixture
{
public:
    std::shared_ptr<toxmm::core> core_a;
    std::shared_ptr<toxmm::core> core_b;
    std::shared_ptr<toxmm::contact> contact_a;
    std::shared_ptr<toxmm::contact> contact_b;

    Glib::RefPtr<Glib::MainLoop> main_loop;
    sigc::connection update_connection;

    const int MAX_WAIT_MIN = 2;
    const int UPDATE_DELAY_MS = 10;

    class MockStorage: public toxmm::storage {
        public:
            MockStorage() {}
            ~MockStorage() {}
            void set_prefix_key(const std::string&) override {}
            void save(const std::initializer_list<std::string>&, const std::vector<uint8_t>&) override {}
            void load(const std::initializer_list<std::string>&, std::vector<uint8_t>&) override {}
    };

    std::shared_ptr<MockStorage> mock_storage;

    GlobalFixture() {
        auto init = [](std::string path, std::string name) {
            toxmm::contactAddrPublic addr;
            auto state = toxmm::core::create_state(name,
                                                   "testing..",
                                                   addr);
            auto file = Gio::File::create_for_path(path);
            try {
                file->remove();
            } catch (...) {
                //ignore
            }
            auto stream = file->create_file();
            auto bytes = Glib::Bytes::create((void*)state.data(), state.size());
            stream->write_bytes(bytes);
            stream->close();
        };

        init("/tmp/gtox_core_a", "Test A");
        init("/tmp/gtox_core_b", "Test B");

        mock_storage = std::make_shared<MockStorage>();
        core_a = toxmm::core::create("/tmp/gtox_core_a", mock_storage);
        core_b = toxmm::core::create("/tmp/gtox_core_b", mock_storage);

        //speed up testing ?
        auto tox_a = core_a->toxcore();
        auto tox_b = core_b->toxcore();

        toxmm::contactAddrPublic  dht_a, dht_b;
        tox_self_get_dht_id(tox_a, dht_a);
        tox_self_get_dht_id(tox_a, dht_b);
        TOX_ERR_GET_PORT error;
        auto port_a = tox_self_get_udp_port(tox_a, &error);
        TS_ASSERT_EQUALS(error, TOX_ERR_GET_PORT_OK);
        auto port_b = tox_self_get_udp_port(tox_b, &error);
        TS_ASSERT_EQUALS(error, TOX_ERR_GET_PORT_OK);
        TOX_ERR_BOOTSTRAP bootstrap_error;
        tox_bootstrap(tox_a, "127.0.0.1", port_b, dht_b, &bootstrap_error);
        TS_ASSERT_EQUALS(bootstrap_error, TOX_ERR_BOOTSTRAP_OK);
        tox_bootstrap(tox_b, "127.0.0.1", port_a, dht_a, &bootstrap_error);
        TS_ASSERT_EQUALS(bootstrap_error, TOX_ERR_BOOTSTRAP_OK);

        update_connection = Glib::signal_timeout().connect(sigc::bind_return([this]() {
            core_a->update();
            core_b->update();
        },true), UPDATE_DELAY_MS);
    }

    ~GlobalFixture() {
        update_connection.disconnect();
    }

    template<typename T>
    void wait_while(T f) {
        int time = 0;
        while (std::chrono::milliseconds(time) < std::chrono::minutes(MAX_WAIT_MIN) &&
               f()) {
            Glib::MainContext::get_default()->iteration(true);
            time += UPDATE_DELAY_MS; //not 100% correct but who cares ?
        }
    }
};

static GlobalFixture gfix;
#endif
