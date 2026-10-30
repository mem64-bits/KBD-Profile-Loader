#if defined(KBD_HAVE_SNI_TRAY)

#include "ui/SniTrayIcon.h"
#include "files/Logger.h"

#include <gio/gio.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace ui {

namespace {

    constexpr const char* ITEM_PATH         = "/StatusNotifierItem";
    constexpr const char* ITEM_INTERFACE    = "org.kde.StatusNotifierItem";
    constexpr const char* MENU_PATH         = "/MenuBar";
    constexpr const char* MENU_INTERFACE    = "com.canonical.dbusmenu";
    constexpr const char* WATCHER_NAME      = "org.kde.StatusNotifierWatcher";
    constexpr const char* WATCHER_PATH      = "/StatusNotifierWatcher";
    constexpr const char* WATCHER_INTERFACE = "org.kde.StatusNotifierWatcher";

    constexpr guint MENU_PROTOCOL_VERSION = 3;

    // https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/ (the properties hosts actually read)
    constexpr const char* ITEM_XML = R"xml(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionMovieName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <method name="ContextMenu"><arg type="i" name="x" direction="in"/><arg type="i" name="y" direction="in"/></method>
    <method name="Activate"><arg type="i" name="x" direction="in"/><arg type="i" name="y" direction="in"/></method>
    <method name="SecondaryActivate"><arg type="i" name="x" direction="in"/><arg type="i" name="y" direction="in"/></method>
    <method name="Scroll"><arg type="i" name="delta" direction="in"/><arg type="s" name="orientation" direction="in"/></method>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewAttentionIcon"/>
    <signal name="NewOverlayIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus"><arg type="s" name="status"/></signal>
  </interface>
</node>)xml";

    // libdbusmenu's com.canonical.dbusmenu interface.
    constexpr const char* MENU_XML = R"xml(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg type="i" name="parentId" direction="in"/>
      <arg type="i" name="recursionDepth" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="u" name="revision" direction="out"/>
      <arg type="(ia{sv}av)" name="layout" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="a(ia{sv})" name="properties" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="name" direction="in"/>
      <arg type="v" name="value" direction="out"/>
    </method>
    <method name="Event">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="eventId" direction="in"/>
      <arg type="v" name="data" direction="in"/>
      <arg type="u" name="timestamp" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg type="a(isvu)" name="events" direction="in"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg type="i" name="id" direction="in"/>
      <arg type="b" name="needUpdate" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="ai" name="updatesNeeded" direction="out"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated">
      <arg type="a(ia{sv})" name="updatedProps"/>
      <arg type="a(ias)" name="removedProps"/>
    </signal>
    <signal name="LayoutUpdated">
      <arg type="u" name="revision"/>
      <arg type="i" name="parent"/>
    </signal>
    <signal name="ItemActivationRequested">
      <arg type="i" name="id"/>
      <arg type="u" name="timestamp"/>
    </signal>
  </interface>
</node>)xml";

    // dbusmenu treats '_' as the mnemonic marker; a literal underscore has to be doubled.
    std::string mnemonicSafe(const std::string& label) {
        std::string escaped;
        escaped.reserve(label.size());
        for (const char c : label) {
            escaped.push_back(c);
            if (c == '_') escaped.push_back('_');
        }
        return escaped;
    }

    GVariant* emptyPixmaps() {
        return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0);
    }

    // a(iiay): width, height and ARGB32 pixels in network byte order (so the bytes are A, R, G, B).
    GVariant* pixmaps(const std::vector<TrayImage>& images) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
        for (const TrayImage& image : images) {
            if (image.width <= 0 || image.height <= 0 ||
                image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
                continue;
            }

            std::vector<guchar> argb(image.rgba.size());
            for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
                argb[i + 0] = image.rgba[i + 3];
                argb[i + 1] = image.rgba[i + 0];
                argb[i + 2] = image.rgba[i + 1];
                argb[i + 3] = image.rgba[i + 2];
            }
            GVariant* bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, argb.data(), argb.size(), sizeof(guchar));
            g_variant_builder_add(&builder, "(ii@ay)", image.width, image.height, bytes);
        }
        return g_variant_builder_end(&builder);
    }

    GVariant* itemProperties(const TrayMenuItem& item) {
        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));

        if (item.kind == TrayMenuItem::Kind::Separator) {
            g_variant_builder_add(&props, "{sv}", "type", g_variant_new_string("separator"));
        } else {
            g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(mnemonicSafe(item.label).c_str()));
            g_variant_builder_add(&props, "{sv}", "enabled",
                                  g_variant_new_boolean(item.enabled && item.kind != TrayMenuItem::Kind::Header));
            if (item.kind == TrayMenuItem::Kind::Check || item.kind == TrayMenuItem::Kind::Radio) {
                g_variant_builder_add(&props, "{sv}", "toggle-type",
                                      g_variant_new_string(item.kind == TrayMenuItem::Kind::Check ? "checkmark" : "radio"));
                g_variant_builder_add(&props, "{sv}", "toggle-state", g_variant_new_int32(item.checked ? 1 : 0));
            }
        }
        g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));
        return g_variant_builder_end(&props);
    }

    // (ia{sv}av): id, properties, children
    GVariant* itemLayout(const TrayMenuItem& item) {
        return g_variant_new("(i@a{sv}@av)", static_cast<gint32>(item.id), itemProperties(item),
                             g_variant_new_array(G_VARIANT_TYPE("v"), nullptr, 0));
    }

    // The root node (id 0) only carries "children-display"; its children are the menu entries.
    GVariant* rootLayout(const TrayMenu& menu, const bool includeChildren) {
        GVariantBuilder rootProps;
        g_variant_builder_init(&rootProps, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&rootProps, "{sv}", "children-display", g_variant_new_string("submenu"));

        GVariantBuilder children;
        g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
        if (includeChildren) {
            for (const TrayMenuItem& item : menu.items) {
                g_variant_builder_add(&children, "v", itemLayout(item));
            }
        }
        return g_variant_new("(i@a{sv}@av)", static_cast<gint32>(0), g_variant_builder_end(&rootProps),
                             g_variant_builder_end(&children));
    }

    bool isClickable(const TrayMenuItem& item) {
        return item.enabled && item.kind != TrayMenuItem::Kind::Separator && item.kind != TrayMenuItem::Kind::Header;
    }

} // namespace

struct SniTrayIcon::Impl {
    Impl(SniTrayIcon& tray, const int instance)
        : owner(tray), busName("org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-" + std::to_string(instance)) {}

    SniTrayIcon&      owner;
    const std::string busName;
    std::jthread      thread;
    std::atomic<bool> running{false};

    GMainContext* context{nullptr};
    GMainLoop*    loop{nullptr};

    // Everything below is only touched on the GLib thread.
    GDBusConnection* connection{nullptr};
    GCancellable*    cancellable{nullptr}; // cancels the pending watcher registration on shutdown
    GDBusNodeInfo*   itemInfo{nullptr};
    GDBusNodeInfo*   menuInfo{nullptr};
    guint            itemRegistration{0};
    guint            menuRegistration{0};
    guint            ownerId{0};
    guint            watcherId{0};
    bool             nameAcquired{false};
    bool             watcherPresent{false};
    bool             registered{false};
    guint            menuRevision{1};

    void run(std::promise<bool> ready);
    void teardown();
    void drain();
    void tryRegister();
    void emitSignal(const char* path, const char* interfaceName, const char* signal, GVariant* parameters);
    void applyChanges(unsigned changes);
    void postChanges(unsigned changes);

    GVariant* itemProperty(const char* name);

    // --- static GDBus callbacks (user_data is the Impl) ---
    static void itemMethodCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                               GVariant*, GDBusMethodInvocation* invocation, gpointer data);
    static GVariant* itemGetProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property,
                                     GError**, gpointer data);
    static void menuMethodCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                               GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data);
    static GVariant* menuGetProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property,
                                     GError**, gpointer data);
    static void onNameAcquired(GDBusConnection*, const gchar*, gpointer data);
    static void onNameLost(GDBusConnection*, const gchar* name, gpointer data);
    static void onWatcherAppeared(GDBusConnection*, const gchar*, const gchar*, gpointer data);
    static void onWatcherVanished(GDBusConnection*, const gchar*, gpointer data);
    static gboolean warnIfNoHost(gpointer data);
};

GVariant* SniTrayIcon::Impl::itemProperty(const char* name) {
    const auto state = owner.state();
    const std::string property = name;

    if (property == "Category") return g_variant_new_string("ApplicationStatus");
    if (property == "Id") return g_variant_new_string("kbd-profile-loader");
    if (property == "Title") return g_variant_new_string(state.title.c_str());
    if (property == "Status") return g_variant_new_string("Active");
    if (property == "IconName" || property == "OverlayIconName" || property == "AttentionIconName" ||
        property == "AttentionMovieName") {
        return g_variant_new_string("");
    }
    if (property == "IconPixmap") return pixmaps(state.icons);
    if (property == "OverlayIconPixmap" || property == "AttentionIconPixmap") return emptyPixmaps();
    if (property == "ToolTip") {
        return g_variant_new("(s@a(iiay)ss)", "", emptyPixmaps(), state.title.c_str(), state.description.c_str());
    }
    if (property == "ItemIsMenu") return g_variant_new_boolean(FALSE);
    if (property == "Menu") return g_variant_new_object_path(MENU_PATH);
    return nullptr;
}

void SniTrayIcon::Impl::itemMethodCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                                       GVariant*, GDBusMethodInvocation* invocation, gpointer data) {
    auto* self = static_cast<Impl*>(data);
    const std::string call = method;

    if (call == "Activate") {
        self->owner.notifyActivated();
    } else if (call == "SecondaryActivate") {
        self->owner.notifySecondaryActivated();
    }
    // ContextMenu / Scroll: the host draws the menu itself from the dbusmenu object; nothing to do.

    g_dbus_method_invocation_return_value(invocation, nullptr);
}

GVariant* SniTrayIcon::Impl::itemGetProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                             const gchar* property, GError** error, gpointer data) {
    GVariant* value = static_cast<Impl*>(data)->itemProperty(property);
    if (value == nullptr) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property);
    }
    return value;
}

void SniTrayIcon::Impl::menuMethodCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                                       GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data) {
    auto* self = static_cast<Impl*>(data);
    const std::string call = method;
    const TrayMenu menu = self->owner.state().menu;

    if (call == "GetLayout") {
        gint32 parentId = 0;
        gint32 depth    = -1;
        g_variant_get_child(parameters, 0, "i", &parentId);
        g_variant_get_child(parameters, 1, "i", &depth);

        if (parentId == 0) {
            // Root: every entry as a leaf child, unless the host asked for no recursion (depth 0).
            GVariant* layout = rootLayout(menu, depth != 0);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", self->menuRevision, layout));
        } else if (const TrayMenuItem* item = menu.find(parentId)) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", self->menuRevision, itemLayout(*item)));
        } else {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Unknown menu item %d", parentId);
        }
    } else if (call == "GetGroupProperties") {
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("a(ia{sv})"));

        GVariantIter* ids = nullptr;
        g_variant_get_child(parameters, 0, "ai", &ids);
        bool any = false;
        gint32 id = 0;
        while (g_variant_iter_next(ids, "i", &id)) {
            any = true;
            if (const TrayMenuItem* item = menu.find(id)) {
                g_variant_builder_add(&result, "(i@a{sv})", static_cast<gint32>(item->id), itemProperties(*item));
            }
        }
        g_variant_iter_free(ids);

        if (!any) { // an empty id list means "all items"
            for (const TrayMenuItem& item : menu.items) {
                g_variant_builder_add(&result, "(i@a{sv})", static_cast<gint32>(item.id), itemProperties(item));
            }
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&result)));
    } else if (call == "GetProperty") {
        gint32 id = 0;
        gchar* name = nullptr;
        g_variant_get(parameters, "(is)", &id, &name);

        const TrayMenuItem* item = menu.find(id);
        GVariant* value = nullptr;
        if (item != nullptr) {
            GVariant* props = itemProperties(*item);
            value = g_variant_lookup_value(props, name, nullptr);
            g_variant_unref(props);
        }
        if (value != nullptr) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(@v)", g_variant_new_variant(value)));
        } else {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No property %s on item %d", name, id);
        }
        g_free(name);
    } else if (call == "Event") {
        gint32 id = 0;
        gchar* eventId = nullptr;
        g_variant_get_child(parameters, 0, "i", &id);
        g_variant_get_child(parameters, 1, "s", &eventId);

        const TrayMenuItem* item = menu.find(id);
        if (item != nullptr && std::string(eventId) == "clicked" && isClickable(*item)) {
            self->owner.notifyMenuItemSelected(id);
        }
        g_free(eventId);
        g_dbus_method_invocation_return_value(invocation, nullptr);
    } else if (call == "EventGroup") {
        GVariantBuilder missing;
        g_variant_builder_init(&missing, G_VARIANT_TYPE("ai"));

        GVariantIter* events = nullptr;
        g_variant_get_child(parameters, 0, "a(isvu)", &events);
        gint32 id = 0;
        gchar* eventId = nullptr;
        GVariant* payload = nullptr;
        guint32 timestamp = 0;
        while (g_variant_iter_next(events, "(is@vu)", &id, &eventId, &payload, &timestamp)) {
            const TrayMenuItem* item = menu.find(id);
            if (item == nullptr) {
                g_variant_builder_add(&missing, "i", id);
            } else if (std::string(eventId) == "clicked" && isClickable(*item)) {
                self->owner.notifyMenuItemSelected(id);
            }
            g_free(eventId);
            g_variant_unref(payload);
        }
        g_variant_iter_free(events);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", g_variant_builder_end(&missing)));
    } else if (call == "AboutToShow") {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
    } else if (call == "AboutToShowGroup") {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@ai@ai)", g_variant_new_array(G_VARIANT_TYPE("i"), nullptr, 0),
                                      g_variant_new_array(G_VARIANT_TYPE("i"), nullptr, 0)));
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method);
    }
}

GVariant* SniTrayIcon::Impl::menuGetProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                             const gchar* property, GError** error, gpointer) {
    const std::string name = property;
    if (name == "Version") return g_variant_new_uint32(MENU_PROTOCOL_VERSION);
    if (name == "TextDirection") return g_variant_new_string("ltr");
    if (name == "Status") return g_variant_new_string("normal");
    if (name == "IconThemePath") return g_variant_new_strv(nullptr, 0);

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property);
    return nullptr;
}

void SniTrayIcon::Impl::emitSignal(const char* path, const char* interfaceName, const char* signal, GVariant* parameters) {
    if (connection == nullptr) {
        if (parameters != nullptr && g_variant_is_floating(parameters)) g_variant_unref(parameters);
        return;
    }

    GError* error = nullptr;
    g_dbus_connection_emit_signal(connection, nullptr, path, interfaceName, signal, parameters, &error);
    if (error != nullptr) {
        LOG_DEBUG("Tray", std::string("Could not emit ") + signal + ": " + error->message);
        g_error_free(error);
    }
}

void SniTrayIcon::Impl::applyChanges(const unsigned changes) {
    if (connection == nullptr) return;

    if (changes & IconChanged) emitSignal(ITEM_PATH, ITEM_INTERFACE, "NewIcon", nullptr);
    if (changes & TooltipChanged) {
        emitSignal(ITEM_PATH, ITEM_INTERFACE, "NewToolTip", nullptr);
        emitSignal(ITEM_PATH, ITEM_INTERFACE, "NewTitle", nullptr);
    }
    if (changes & MenuChanged) {
        ++menuRevision;
        emitSignal(MENU_PATH, MENU_INTERFACE, "LayoutUpdated", g_variant_new("(ui)", menuRevision, static_cast<gint32>(0)));
    }
}

void SniTrayIcon::Impl::postChanges(const unsigned changes) {
    struct Pending {
        Impl*    impl;
        unsigned changes;
    };

    // Runs on the GLib thread (immediately if the caller already is that thread).
    g_main_context_invoke_full(
        context, G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
            const auto* pending = static_cast<Pending*>(data);
            pending->impl->applyChanges(pending->changes);
            return G_SOURCE_REMOVE;
        },
        new Pending{this, changes},
        [](gpointer data) { delete static_cast<Pending*>(data); });
}

void SniTrayIcon::Impl::tryRegister() {
    if (!nameAcquired || !watcherPresent || registered || connection == nullptr) return;
    registered = true;

    g_dbus_connection_call(
        connection, WATCHER_NAME, WATCHER_PATH, WATCHER_INTERFACE, "RegisterStatusNotifierItem",
        g_variant_new("(s)", busName.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, cancellable,
        [](GObject* source, GAsyncResult* result, gpointer data) {
            auto* self = static_cast<Impl*>(data);
            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            if (reply != nullptr) {
                g_variant_unref(reply);
                LOG_INFO("Tray", "Registered the tray icon with the StatusNotifierWatcher.");
            } else if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                // shutting down: nothing to report
            } else {
                LOG_WARN("Tray", std::string("StatusNotifierWatcher refused the tray icon: ") + (error ? error->message : "unknown error"));
                self->registered = false;
            }
            if (error != nullptr) g_error_free(error);
        },
        this);
}

void SniTrayIcon::Impl::onNameAcquired(GDBusConnection*, const gchar*, gpointer data) {
    auto* self = static_cast<Impl*>(data);
    self->nameAcquired = true;
    self->tryRegister();
}

void SniTrayIcon::Impl::onNameLost(GDBusConnection*, const gchar* name, gpointer data) {
    auto* self = static_cast<Impl*>(data);
    self->nameAcquired = false;
    self->registered   = false;
    LOG_WARN("Tray", std::string("Lost the D-Bus name ") + (name != nullptr ? name : "(none)"));
}

void SniTrayIcon::Impl::onWatcherAppeared(GDBusConnection*, const gchar*, const gchar*, gpointer data) {
    auto* self = static_cast<Impl*>(data);
    self->watcherPresent = true;
    self->registered     = false; // a new watcher instance knows nothing about us yet
    self->tryRegister();
}

void SniTrayIcon::Impl::onWatcherVanished(GDBusConnection*, const gchar*, gpointer data) {
    auto* self = static_cast<Impl*>(data);
    self->watcherPresent = false;
    self->registered     = false;
}

gboolean SniTrayIcon::Impl::warnIfNoHost(gpointer data) {
    const auto* self = static_cast<Impl*>(data);
    if (!self->watcherPresent) {
        LOG_WARN("Tray",
                 "No StatusNotifierWatcher on the session bus, so there is no tray to show the icon in yet. It will "
                 "appear as soon as a tray host starts (KDE Plasma, GNOME with the AppIndicator extension, waybar, ...). "
                 "Meanwhile `KBD_Profile_Loader --show` opens the dashboard.");
    }
    return G_SOURCE_REMOVE;
}

void SniTrayIcon::Impl::run(std::promise<bool> ready) {
    g_main_context_push_thread_default(context);

    GError* error = nullptr;
    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (connection == nullptr) {
        LOG_WARN("Tray", std::string("No D-Bus session bus, so no tray icon: ") + (error ? error->message : "unknown error"));
        if (error != nullptr) g_error_free(error);
        g_main_context_pop_thread_default(context);
        ready.set_value(false);
        return;
    }

    cancellable = g_cancellable_new();

    // GDBus' default for bus connections is to exit() the whole process when the bus goes away. A missing
    // tray must never take the keyboard daemon down with it, so the icon just disappears instead.
    g_dbus_connection_set_exit_on_close(connection, FALSE);

    itemInfo = g_dbus_node_info_new_for_xml(ITEM_XML, nullptr);
    menuInfo = g_dbus_node_info_new_for_xml(MENU_XML, nullptr);
    if (itemInfo == nullptr || menuInfo == nullptr) {
        LOG_ERROR("Tray", "Internal error: the tray's D-Bus interface description is invalid.");
        teardown();
        g_main_context_pop_thread_default(context);
        ready.set_value(false);
        return;
    }

    static const GDBusInterfaceVTable itemVTable = {&Impl::itemMethodCall, &Impl::itemGetProperty, nullptr, {nullptr}};
    static const GDBusInterfaceVTable menuVTable = {&Impl::menuMethodCall, &Impl::menuGetProperty, nullptr, {nullptr}};

    itemRegistration = g_dbus_connection_register_object(connection, ITEM_PATH, itemInfo->interfaces[0], &itemVTable, this, nullptr, &error);
    if (itemRegistration != 0) {
        menuRegistration = g_dbus_connection_register_object(connection, MENU_PATH, menuInfo->interfaces[0], &menuVTable, this, nullptr, &error);
    }
    if (itemRegistration == 0 || menuRegistration == 0) {
        LOG_ERROR("Tray", std::string("Could not export the tray objects on D-Bus: ") + (error ? error->message : "unknown error"));
        if (error != nullptr) g_error_free(error);
        teardown();
        g_main_context_pop_thread_default(context);
        ready.set_value(false);
        return;
    }

    ownerId   = g_bus_own_name_on_connection(connection, busName.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE,
                                             &Impl::onNameAcquired, &Impl::onNameLost, this, nullptr);
    watcherId = g_bus_watch_name_on_connection(connection, WATCHER_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                               &Impl::onWatcherAppeared, &Impl::onWatcherVanished, this, nullptr);

    GSource* hostCheck = g_timeout_source_new_seconds(3);
    g_source_set_callback(hostCheck, &Impl::warnIfNoHost, this, nullptr);
    g_source_attach(hostCheck, context);
    g_source_unref(hostCheck);

    running.store(true);
    ready.set_value(true);

    g_main_loop_run(loop);

    running.store(false);
    teardown();
    g_main_context_pop_thread_default(context);
}

void SniTrayIcon::Impl::drain() {
    // GIO finishes several operations asynchronously (releasing the bus name, cancelled calls) by queueing
    // work on this thread's main context. Run it until it has been quiet for a moment, otherwise those
    // operations never complete and keep the shared session-bus connection alive forever.
    const gint64 deadline = g_get_monotonic_time() + 500 * G_TIME_SPAN_MILLISECOND;
    int quietRounds = 0;
    while (g_get_monotonic_time() < deadline && quietRounds < 5) {
        if (g_main_context_iteration(context, FALSE)) {
            quietRounds = 0;
        } else {
            ++quietRounds;
            g_usleep(10 * 1000);
        }
    }
}

void SniTrayIcon::Impl::teardown() {
    if (cancellable != nullptr) g_cancellable_cancel(cancellable);
    if (watcherId != 0) {
        g_bus_unwatch_name(watcherId);
        watcherId = 0;
    }
    if (ownerId != 0) {
        g_bus_unown_name(ownerId); // releases the name, so the host drops our icon right away
        ownerId = 0;
    }
    if (connection != nullptr) {
        if (itemRegistration != 0) g_dbus_connection_unregister_object(connection, itemRegistration);
        if (menuRegistration != 0) g_dbus_connection_unregister_object(connection, menuRegistration);
        itemRegistration = menuRegistration = 0;
        drain();
    }
    if (itemInfo != nullptr) {
        g_dbus_node_info_unref(itemInfo);
        itemInfo = nullptr;
    }
    if (menuInfo != nullptr) {
        g_dbus_node_info_unref(menuInfo);
        menuInfo = nullptr;
    }
    if (connection != nullptr) {
        g_object_unref(connection);
        connection = nullptr;
    }
    if (cancellable != nullptr) {
        g_object_unref(cancellable);
        cancellable = nullptr;
    }
    nameAcquired = watcherPresent = registered = false;
}

SniTrayIcon::SniTrayIcon(const int instanceNumber) : m_impl(std::make_unique<Impl>(*this, instanceNumber)) {}

SniTrayIcon::~SniTrayIcon() {
    stop();
}

bool SniTrayIcon::start() {
    if (m_impl->running.load()) return true;
    if (m_impl->thread.joinable()) return false;

    m_impl->context = g_main_context_new();
    m_impl->loop    = g_main_loop_new(m_impl->context, FALSE);

    std::promise<bool> ready;
    std::future<bool> result = ready.get_future();
    m_impl->thread = std::jthread([impl = m_impl.get(), promise = std::move(ready)](const std::stop_token&) mutable {
        impl->run(std::move(promise));
    });

    const bool started = result.wait_for(std::chrono::seconds(10)) == std::future_status::ready && result.get();
    if (!started) stop();
    return started;
}

void SniTrayIcon::stop() {
    if (m_impl->thread.joinable()) {
        if (m_impl->loop != nullptr) g_main_loop_quit(m_impl->loop);
        m_impl->thread.join();
    }

    if (m_impl->loop != nullptr) {
        g_main_loop_unref(m_impl->loop);
        m_impl->loop = nullptr;
    }
    if (m_impl->context != nullptr) {
        g_main_context_unref(m_impl->context);
        m_impl->context = nullptr;
    }
}

bool SniTrayIcon::isRunning() const noexcept {
    return m_impl->running.load();
}

std::string_view SniTrayIcon::backendName() const noexcept {
    return "sni";
}

std::string SniTrayIcon::serviceName() const {
    return m_impl->busName;
}

void SniTrayIcon::onStateChanged(const unsigned changes) {
    if (m_impl->running.load()) m_impl->postChanges(changes);
}

} // namespace ui

#endif // KBD_HAVE_SNI_TRAY
