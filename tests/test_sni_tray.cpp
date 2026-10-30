#include <gtest/gtest.h>

#if defined(KBD_HAVE_SNI_TRAY)

#include "support/Fakes.h"
#include "ui/SniTrayIcon.h"
#include "ui/ThemeManager.h"
#include "ui/TrayIconRenderer.h"
#include "ui/TrayMenuBuilder.h"

#include <gio/gio.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <thread>

// Integration tests for the Linux tray. A private dbus-daemon is started (GTestDBus) and a MockTrayHost
// plays the desktop: it owns org.kde.StatusNotifierWatcher and records the items that register. The
// tests then talk to the tray exactly like Plasma / waybar / the GNOME AppIndicator extension would.

using namespace std::chrono_literals;

namespace {

    constexpr const char* WATCHER_XML = R"xml(
<node>
  <interface name="org.kde.StatusNotifierWatcher">
    <method name="RegisterStatusNotifierItem"><arg type="s" name="service" direction="in"/></method>
    <method name="RegisterStatusNotifierHost"><arg type="s" name="service" direction="in"/></method>
    <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
    <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
    <property name="ProtocolVersion" type="i" access="read"/>
  </interface>
</node>)xml";

    template <typename Predicate>
    bool waitUntil(Predicate&& predicate, const std::chrono::milliseconds timeout = 5000ms) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) return true;
            std::this_thread::sleep_for(5ms);
        }
        return predicate();
    }

    GDBusConnection* connectToBus(const std::string& address) {
        GError* error = nullptr;
        GDBusConnection* connection = g_dbus_connection_new_for_address_sync(
            address.c_str(),
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
        if (error != nullptr) {
            ADD_FAILURE() << "cannot connect to the private bus: " << error->message;
            g_error_free(error);
        }
        return connection;
    }

    // The desktop side of the StatusNotifier protocol, on its own thread and main context.
    class MockTrayHost {
    public:
        explicit MockTrayHost(std::string address) : m_address(std::move(address)) {}
        ~MockTrayHost() { stop(); }

        MockTrayHost(const MockTrayHost&) = delete;
        MockTrayHost& operator=(const MockTrayHost&) = delete;

        bool start() {
            if (m_thread.joinable()) return false;
            m_ready = std::promise<bool>();
            std::future<bool> result = m_ready.get_future();
            m_thread = std::jthread([this](std::stop_token) { run(); });
            return result.wait_for(5s) == std::future_status::ready && result.get();
        }

        // The "desktop panel" going away: releases org.kde.StatusNotifierWatcher.
        void stop() {
            if (!m_thread.joinable()) return;
            g_main_loop_quit(m_loop);
            m_thread.join();
        }

        [[nodiscard]] std::vector<std::string> registered() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_registered;
        }

    private:
        static void onCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                           GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data) {
            auto* self = static_cast<MockTrayHost*>(data);
            if (std::string(method) == "RegisterStatusNotifierItem") {
                gchar* service = nullptr;
                g_variant_get(parameters, "(s)", &service);
                {
                    std::lock_guard<std::mutex> lock(self->m_mutex);
                    self->m_registered.emplace_back(service);
                }
                g_free(service);
            }
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }

        static void onNameAcquired(GDBusConnection*, const gchar*, gpointer data) {
            static_cast<MockTrayHost*>(data)->m_ready.set_value(true);
        }

        static void onNameLost(GDBusConnection*, const gchar*, gpointer data) {
            auto* self = static_cast<MockTrayHost*>(data);
            try { self->m_ready.set_value(false); } catch (const std::future_error&) {}
        }

        void run() {
            GMainContext* context = g_main_context_new();
            g_main_context_push_thread_default(context);
            m_loop = g_main_loop_new(context, FALSE);

            GDBusConnection* connection = connectToBus(m_address);
            GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(WATCHER_XML, nullptr);
            static const GDBusInterfaceVTable vtable = {&MockTrayHost::onCall, nullptr, nullptr, {nullptr}};
            const guint registration = g_dbus_connection_register_object(connection, "/StatusNotifierWatcher",
                                                                         info->interfaces[0], &vtable, this, nullptr, nullptr);
            const guint owner = g_bus_own_name_on_connection(connection, "org.kde.StatusNotifierWatcher",
                                                             G_BUS_NAME_OWNER_FLAGS_NONE, &MockTrayHost::onNameAcquired,
                                                             &MockTrayHost::onNameLost, this, nullptr);
            g_main_loop_run(m_loop);

            g_bus_unown_name(owner);
            g_dbus_connection_unregister_object(connection, registration);
            g_dbus_node_info_unref(info);
            g_dbus_connection_close_sync(connection, nullptr, nullptr);
            g_object_unref(connection);
            g_main_loop_unref(m_loop);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context);
        }

        std::string               m_address;
        std::jthread              m_thread;
        std::promise<bool>        m_ready;
        GMainLoop*                m_loop{nullptr};
        mutable std::mutex        m_mutex;
        std::vector<std::string>  m_registered;
    };

    // Records what the tray tells its observers.
    class RecordingObserver : public ui::ITrayObserver {
    public:
        void onTrayActivated() override { ++activated; }
        void onTraySecondaryActivated() override { ++secondary; }
        void onTrayMenuItemSelected(const int id) override {
            std::lock_guard<std::mutex> lock(mutex);
            chosen.push_back(id);
        }

        [[nodiscard]] std::vector<int> chosenItems() const {
            std::lock_guard<std::mutex> lock(mutex);
            return chosen;
        }

        std::atomic<int>   activated{0};
        std::atomic<int>   secondary{0};
        mutable std::mutex mutex;
        std::vector<int>   chosen;
    };

    class SniTrayTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            gchar* daemon = g_find_program_in_path("dbus-daemon"); // allocated: must be freed
            if (daemon == nullptr) return;
            g_free(daemon);
            s_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
            g_test_dbus_up(s_bus);
        }

        static void TearDownTestSuite() {
            if (s_bus == nullptr) return;
            g_test_dbus_down(s_bus);
            g_object_unref(s_bus);
            s_bus = nullptr;
        }

        void SetUp() override {
            if (s_bus == nullptr) GTEST_SKIP() << "dbus-daemon is not installed";
            m_address = g_test_dbus_get_bus_address(s_bus);
            m_client  = connectToBus(m_address);
            ASSERT_NE(m_client, nullptr);

            tray.addObserver(&observer);
            tray.setMenu(menu());
            tray.setTooltip("Keychron K2 HE", "Slot 2: Tekken_8 (Wired USB-C)");
            tray.setIcons(ui::renderTrayIcons(ui::Theme::blackPurple(), 1, true));
        }

        void TearDown() override {
            tray.removeObserver(&observer);
            tray.stop();
            if (m_client != nullptr) {
                g_dbus_connection_close_sync(m_client, nullptr, nullptr);
                g_object_unref(m_client);
            }
        }

        static ui::TrayMenu menu() {
            ui::AppSnapshot state;
            state.device = {true, kbd::TransportMode::Wired, "path"};
            state.activeSlot = 1;
            state.slots[0] = {true, "Default", "default.json", std::nullopt};
            state.slots[1] = {true, "Tekken_8", "tekken.json", std::nullopt}; // underscore: must be escaped for dbusmenu
            return ui::tray::buildMenu(state, true);
        }

        GVariant* call(const std::string& path, const std::string& interfaceName, const std::string& method, GVariant* parameters,
                       const char* replyType = nullptr) {
            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_sync(m_client, tray.serviceName().c_str(), path.c_str(), interfaceName.c_str(),
                                                          method.c_str(), parameters,
                                                          replyType != nullptr ? G_VARIANT_TYPE(replyType) : nullptr,
                                                          G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
            if (error != nullptr) {
                ADD_FAILURE() << method << " failed: " << error->message;
                g_error_free(error);
            }
            return reply;
        }

        // Properties.GetAll as a name -> value map (values are the unwrapped GVariants; caller owns the dict).
        GVariant* allItemProperties() {
            GVariant* reply = call("/StatusNotifierItem", "org.freedesktop.DBus.Properties", "GetAll",
                                   g_variant_new("(s)", "org.kde.StatusNotifierItem"), "(a{sv})");
            if (reply == nullptr) return nullptr;
            GVariant* dict = g_variant_get_child_value(reply, 0);
            g_variant_unref(reply);
            return dict;
        }

        static std::string stringProperty(GVariant* dict, const char* name) {
            GVariant* value = g_variant_lookup_value(dict, name, G_VARIANT_TYPE_STRING);
            if (value == nullptr) return "<missing>";
            const std::string text = g_variant_get_string(value, nullptr);
            g_variant_unref(value);
            return text;
        }

        // Layout children as id -> properties dict.
        std::map<int, GVariant*> layoutChildren(GVariant* layout) {
            std::map<int, GVariant*> children;
            GVariant* list = g_variant_get_child_value(layout, 2); // av
            for (gsize i = 0; i < g_variant_n_children(list); ++i) {
                GVariant* wrapped = g_variant_get_child_value(list, i); // v
                GVariant* node = g_variant_get_variant(wrapped);        // (ia{sv}av)
                gint32 id = 0;
                g_variant_get_child(node, 0, "i", &id);
                children[id] = g_variant_get_child_value(node, 1);
                g_variant_unref(node);
                g_variant_unref(wrapped);
            }
            g_variant_unref(list);
            return children;
        }

        inline static GTestDBus*   s_bus = nullptr;
        std::string                m_address;
        GDBusConnection*           m_client = nullptr;
        ui::SniTrayIcon            tray;
        RecordingObserver          observer;
    };

} // namespace

TEST_F(SniTrayTest, RegistersItsServiceNameWithTheDesktopHost) {
    MockTrayHost host(m_address);
    ASSERT_TRUE(host.start());

    ASSERT_TRUE(tray.start());

    EXPECT_TRUE(tray.isRunning());
    EXPECT_EQ(tray.backendName(), "sni");
    ASSERT_TRUE(waitUntil([&] { return !host.registered().empty(); }));
    EXPECT_EQ(host.registered().front(), tray.serviceName());
    EXPECT_EQ(tray.serviceName().rfind("org.kde.StatusNotifierItem-", 0), 0u);
}

TEST_F(SniTrayTest, RegistersLateWhenTheDesktopHostStartsAfterTheIcon) {
    ASSERT_TRUE(tray.start()) << "starting without any tray host must still succeed";

    MockTrayHost host(m_address);
    ASSERT_TRUE(host.start());

    EXPECT_TRUE(waitUntil([&] { return !host.registered().empty(); }));
}

TEST_F(SniTrayTest, RegistersAgainWhenTheDesktopHostRestarts) {
    MockTrayHost first(m_address);
    ASSERT_TRUE(first.start());
    ASSERT_TRUE(tray.start());
    ASSERT_TRUE(waitUntil([&] { return first.registered().size() == 1; }));

    first.stop(); // the panel crashes ...
    MockTrayHost second(m_address);
    ASSERT_TRUE(second.start()); // ... and comes back

    EXPECT_TRUE(waitUntil([&] { return second.registered().size() == 1; }));
}

TEST_F(SniTrayTest, ExportsTheStatusNotifierItemProperties) {
    ASSERT_TRUE(tray.start());

    GVariant* props = allItemProperties();
    ASSERT_NE(props, nullptr);

    EXPECT_EQ(stringProperty(props, "Category"), "ApplicationStatus");
    EXPECT_EQ(stringProperty(props, "Id"), "kbd-profile-loader");
    EXPECT_EQ(stringProperty(props, "Status"), "Active");
    EXPECT_EQ(stringProperty(props, "Title"), "Keychron K2 HE");

    GVariant* menuPath = g_variant_lookup_value(props, "Menu", G_VARIANT_TYPE_OBJECT_PATH);
    ASSERT_NE(menuPath, nullptr);
    EXPECT_STREQ(g_variant_get_string(menuPath, nullptr), "/MenuBar");
    g_variant_unref(menuPath);

    GVariant* isMenu = g_variant_lookup_value(props, "ItemIsMenu", G_VARIANT_TYPE_BOOLEAN);
    ASSERT_NE(isMenu, nullptr);
    EXPECT_FALSE(g_variant_get_boolean(isMenu)) << "left click must call Activate";
    g_variant_unref(isMenu);

    // ToolTip: (icon name, icon pixmaps, title, description)
    GVariant* tooltip = g_variant_lookup_value(props, "ToolTip", nullptr);
    ASSERT_NE(tooltip, nullptr);
    const char *title = nullptr, *description = nullptr;
    g_variant_get_child(tooltip, 2, "&s", &title);
    g_variant_get_child(tooltip, 3, "&s", &description);
    EXPECT_STREQ(title, "Keychron K2 HE");
    EXPECT_STREQ(description, "Slot 2: Tekken_8 (Wired USB-C)");
    g_variant_unref(tooltip);

    g_variant_unref(props);
}

TEST_F(SniTrayTest, IconPixmapsAreArgbInNetworkByteOrderForEverySize) {
    ASSERT_TRUE(tray.start());
    GVariant* props = allItemProperties();
    ASSERT_NE(props, nullptr);

    GVariant* pixmaps = g_variant_lookup_value(props, "IconPixmap", nullptr);
    ASSERT_NE(pixmaps, nullptr);
    ASSERT_EQ(g_variant_n_children(pixmaps), ui::TRAY_ICON_SIZES.size());

    const auto expectedIcons = ui::renderTrayIcons(ui::Theme::blackPurple(), 1, true);
    for (gsize i = 0; i < g_variant_n_children(pixmaps); ++i) {
        gint32 width = 0, height = 0;
        GVariant* bytes = nullptr;
        GVariant* entry = g_variant_get_child_value(pixmaps, i);
        g_variant_get(entry, "(ii@ay)", &width, &height, &bytes);
        EXPECT_EQ(width, expectedIcons[i].width);
        EXPECT_EQ(height, expectedIcons[i].height);

        gsize length = 0;
        const auto* data = static_cast<const guchar*>(g_variant_get_fixed_array(bytes, &length, 1));
        ASSERT_EQ(length, expectedIcons[i].rgba.size());

        // Pixel at the middle of the top edge is the purple rim: opaque, so bytes are A, R, G, B.
        const std::size_t top = (static_cast<std::size_t>(width / 2)) * 4;
        const ui::Color accent = ui::Theme::blackPurple().palette.accent;
        EXPECT_EQ(data[top + 0], 255);
        EXPECT_EQ(data[top + 1], accent.r);
        EXPECT_EQ(data[top + 2], accent.g);
        EXPECT_EQ(data[top + 3], accent.b);
        // Corner pixel is transparent.
        EXPECT_EQ(data[0], 0);

        g_variant_unref(bytes);
        g_variant_unref(entry);
    }
    g_variant_unref(pixmaps);
    g_variant_unref(props);
}

TEST_F(SniTrayTest, MenuLayoutMirrorsTheMenuModel) {
    ASSERT_TRUE(tray.start());
    const ui::TrayMenu expected = menu();

    GVariant* reply = call("/MenuBar", "com.canonical.dbusmenu", "GetLayout",
                           g_variant_new("(ii@as)", 0, -1, g_variant_new_strv(nullptr, 0)), "(u(ia{sv}av))");
    ASSERT_NE(reply, nullptr);
    GVariant* layout = g_variant_get_child_value(reply, 1);
    gint32 rootId = -1;
    g_variant_get_child(layout, 0, "i", &rootId);
    EXPECT_EQ(rootId, 0);

    auto children = layoutChildren(layout);
    ASSERT_EQ(children.size(), expected.items.size());

    for (const auto& item : expected.items) {
        ASSERT_TRUE(children.count(item.id)) << "missing item " << item.id;
        GVariant* props = children[item.id];

        if (item.kind == ui::TrayMenuItem::Kind::Separator) {
            EXPECT_EQ(stringProperty(props, "type"), "separator");
            continue;
        }
        std::string label = item.label;
        for (std::size_t pos = label.find('_'); pos != std::string::npos; pos = label.find('_', pos + 2)) label.insert(pos, "_");
        EXPECT_EQ(stringProperty(props, "label"), label);

        GVariant* enabled = g_variant_lookup_value(props, "enabled", G_VARIANT_TYPE_BOOLEAN);
        ASSERT_NE(enabled, nullptr);
        EXPECT_EQ(g_variant_get_boolean(enabled) != FALSE, item.enabled && item.kind != ui::TrayMenuItem::Kind::Header) << item.label;
        g_variant_unref(enabled);

        if (item.kind == ui::TrayMenuItem::Kind::Radio || item.kind == ui::TrayMenuItem::Kind::Check) {
            EXPECT_EQ(stringProperty(props, "toggle-type"), item.kind == ui::TrayMenuItem::Kind::Radio ? "radio" : "checkmark");
            GVariant* state = g_variant_lookup_value(props, "toggle-state", G_VARIANT_TYPE_INT32);
            ASSERT_NE(state, nullptr);
            EXPECT_EQ(g_variant_get_int32(state), item.checked ? 1 : 0) << item.label;
            g_variant_unref(state);
        }
    }

    for (auto& [id, props] : children) g_variant_unref(props);
    g_variant_unref(layout);
    g_variant_unref(reply);
}

TEST_F(SniTrayTest, UnderscoresInLabelsAreEscapedSoTheyAreNotTreatedAsMnemonics) {
    ASSERT_TRUE(tray.start());

    GVariant* reply = call("/MenuBar", "com.canonical.dbusmenu", "GetProperty",
                           g_variant_new("(is)", ui::tray::idFor(ui::TrayAction::ActivateSlot2), "label"), "(v)");
    ASSERT_NE(reply, nullptr);
    GVariant* wrapped = g_variant_get_child_value(reply, 0);
    GVariant* label = g_variant_get_variant(wrapped);
    EXPECT_STREQ(g_variant_get_string(label, nullptr), "Slot 2 - Tekken__8");
    g_variant_unref(label);
    g_variant_unref(wrapped);
    g_variant_unref(reply);
}

TEST_F(SniTrayTest, GroupPropertiesReturnTheRequestedItemsOrAllWhenEmpty) {
    ASSERT_TRUE(tray.start());
    const auto expected = menu();

    GVariantBuilder ids;
    g_variant_builder_init(&ids, G_VARIANT_TYPE("ai"));
    g_variant_builder_add(&ids, "i", ui::tray::idFor(ui::TrayAction::Quit));
    GVariant* some = call("/MenuBar", "com.canonical.dbusmenu", "GetGroupProperties",
                          g_variant_new("(@ai@as)", g_variant_builder_end(&ids), g_variant_new_strv(nullptr, 0)), "(a(ia{sv}))");
    ASSERT_NE(some, nullptr);
    GVariant* someList = g_variant_get_child_value(some, 0);
    EXPECT_EQ(g_variant_n_children(someList), 1u);
    g_variant_unref(someList);
    g_variant_unref(some);

    GVariant* all = call("/MenuBar", "com.canonical.dbusmenu", "GetGroupProperties",
                         g_variant_new("(@ai@as)", g_variant_new_array(G_VARIANT_TYPE("i"), nullptr, 0), g_variant_new_strv(nullptr, 0)),
                         "(a(ia{sv}))");
    ASSERT_NE(all, nullptr);
    GVariant* allList = g_variant_get_child_value(all, 0);
    EXPECT_EQ(g_variant_n_children(allList), expected.items.size());
    g_variant_unref(allList);
    g_variant_unref(all);
}

TEST_F(SniTrayTest, ClickedEventsReachTheObserverButOnlyForClickableEntries) {
    ASSERT_TRUE(tray.start());
    const auto send = [&](const int id, const char* event) {
        GVariant* reply = call("/MenuBar", "com.canonical.dbusmenu", "Event",
                               g_variant_new("(isvu)", id, event, g_variant_new_int32(0), 0u), "()");
        if (reply != nullptr) g_variant_unref(reply);
    };

    send(ui::tray::idFor(ui::TrayAction::NextProfile), "clicked");
    send(ui::tray::idFor(ui::TrayAction::Quit), "hovered");     // not a click
    send(200, "clicked");                                         // a header
    send(100, "clicked");                                         // a separator
    send(987654, "clicked");                                      // unknown id
    send(ui::tray::idFor(ui::TrayAction::Quit), "clicked");

    ASSERT_TRUE(waitUntil([&] { return observer.chosenItems().size() >= 2; }));
    EXPECT_EQ(observer.chosenItems(), (std::vector<int>{ui::tray::idFor(ui::TrayAction::NextProfile), ui::tray::idFor(ui::TrayAction::Quit)}));
}

TEST_F(SniTrayTest, ActivateAndSecondaryActivateReachTheObserver) {
    ASSERT_TRUE(tray.start());

    GVariant* a = call("/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate", g_variant_new("(ii)", 10, 20), "()");
    GVariant* b = call("/StatusNotifierItem", "org.kde.StatusNotifierItem", "SecondaryActivate", g_variant_new("(ii)", 10, 20), "()");
    if (a) g_variant_unref(a);
    if (b) g_variant_unref(b);

    EXPECT_TRUE(waitUntil([&] { return observer.activated == 1 && observer.secondary == 1; }));
}

TEST_F(SniTrayTest, StateChangesEmitSignalsAndBumpTheMenuRevision) {
    ASSERT_TRUE(tray.start());

    std::atomic<int> newIcon{0}, newToolTip{0}, layoutUpdated{0};
    struct Counters { std::atomic<int>* icon; std::atomic<int>* tip; std::atomic<int>* layout; } counters{&newIcon, &newToolTip, &layoutUpdated};
    const auto onSignal = [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signal, GVariant*, gpointer data) {
        auto* c = static_cast<Counters*>(data);
        const std::string name = signal;
        if (name == "NewIcon") ++*c->icon;
        if (name == "NewToolTip") ++*c->tip;
        if (name == "LayoutUpdated") ++*c->layout;
    };
    const guint s1 = g_dbus_connection_signal_subscribe(m_client, tray.serviceName().c_str(), nullptr, nullptr, nullptr, nullptr,
                                                        G_DBUS_SIGNAL_FLAGS_NONE, onSignal, &counters, nullptr);

    const auto pump = [&](const auto& done) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!done() && std::chrono::steady_clock::now() < deadline) {
            while (g_main_context_iteration(nullptr, FALSE)) {}
            std::this_thread::sleep_for(2ms);
        }
        return done();
    };

    GVariant* before = call("/MenuBar", "com.canonical.dbusmenu", "GetLayout",
                            g_variant_new("(ii@as)", 0, 0, g_variant_new_strv(nullptr, 0)), "(u(ia{sv}av))");
    guint32 revisionBefore = 0;
    g_variant_get_child(before, 0, "u", &revisionBefore);
    g_variant_unref(before);

    tray.setTooltip("Keychron K2 HE", "Not connected");
    tray.setIcons(ui::renderTrayIcons(ui::Theme::blackPurple(), 1, false));
    auto smaller = menu();
    smaller.items.pop_back();
    tray.setMenu(smaller);

    EXPECT_TRUE(pump([&] { return newIcon >= 1 && newToolTip >= 1 && layoutUpdated >= 1; }));

    GVariant* after = call("/MenuBar", "com.canonical.dbusmenu", "GetLayout",
                           g_variant_new("(ii@as)", 0, -1, g_variant_new_strv(nullptr, 0)), "(u(ia{sv}av))");
    guint32 revisionAfter = 0;
    g_variant_get_child(after, 0, "u", &revisionAfter);
    GVariant* layout = g_variant_get_child_value(after, 1);
    auto children = layoutChildren(layout);
    EXPECT_GT(revisionAfter, revisionBefore);
    EXPECT_EQ(children.size(), smaller.items.size());
    for (auto& [id, props] : children) g_variant_unref(props);
    g_variant_unref(layout);
    g_variant_unref(after);

    GVariant* props = allItemProperties();
    GVariant* tooltip = g_variant_lookup_value(props, "ToolTip", nullptr);
    const char* description = nullptr;
    g_variant_get_child(tooltip, 3, "&s", &description);
    EXPECT_STREQ(description, "Not connected");
    g_variant_unref(tooltip);
    g_variant_unref(props);

    g_dbus_connection_signal_unsubscribe(m_client, s1);
}

TEST_F(SniTrayTest, StopReleasesTheBusNameSoTheHostDropsTheIcon) {
    ASSERT_TRUE(tray.start());
    ASSERT_NE(allItemProperties(), nullptr);

    tray.stop();

    EXPECT_FALSE(tray.isRunning());
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(m_client, tray.serviceName().c_str(), "/StatusNotifierItem",
                                                  "org.freedesktop.DBus.Properties", "GetAll",
                                                  g_variant_new("(s)", "org.kde.StatusNotifierItem"), nullptr,
                                                  G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
    EXPECT_EQ(reply, nullptr);
    EXPECT_NE(error, nullptr);
    if (error != nullptr) g_error_free(error);

    EXPECT_TRUE(tray.start()) << "a stopped tray can be started again";
}

#else // !KBD_HAVE_SNI_TRAY

TEST(SniTrayTest, SkippedBecauseGioWasNotFoundAtConfigureTime) {
    GTEST_SKIP() << "built without KBD_HAVE_SNI_TRAY";
}

#endif
