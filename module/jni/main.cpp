#include "zygisk.hpp"
#include <android/log.h>
#include <dlfcn.h>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "MyZygiskModule", __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

class MyZygiskModule : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        // Get package name of the app being spawned
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        std::string pkg(package_name);

        // List of target apps (replace with your specific package names)
        std::vector<std::string> target_apps = {
            "com.target.app1",
            "com.target.app2"
        };

        bool is_target_app = false;
        for (const auto& target : target_apps) {
            if (pkg == target) {
                is_target_app = true;
                break;
            }
        }

        env->ReleaseStringUTFChars(args->nice_name, package_name);

        if (is_target_app) {
            // Keep module loaded for this app
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            hookProperties();
        }
    }

private:
    Api* api;
    JNIEnv* env;

    // Original function pointers
    using property_get_t = int (*)(const char*, char*, const char*);
    using property_list_t = void (*)(void (*)(const char*, const char*, void*), void*);

    static property_get_t orig_property_get;
    static property_list_t orig_property_list;

    // Hooked property_get
    static int hooked_property_get(const char* name, char* value, const char* default_value) {
        if (name && strncmp(name, "persist.sys.pihooks", 19) == 0) {
            // Return empty string and indicate property doesn't exist
            value[0] = '\0';
            return 0;
        }
        return orig_property_get(name, value, default_value);
    }

    // Callback for property_list to filter properties
    static void filter_property_callback(const char* name, const char* value, void* cookie) {
        auto* original_callback = (void (*)(const char*, const char*, void*))cookie;
        if (name && strncmp(name, "persist.sys.pihooks", 19) != 0) {
            original_callback(name, value, nullptr);
        }
    }

    // Hooked property_list
    static void hooked_property_list(void (*callback)(const char*, const char*, void*), void* cookie) {
        orig_property_list(filter_property_callback, (void*)callback);
    }

    void hookProperties() {
        // Hook property_get (used by SystemProperties.get and getprop for individual lookups)
        void* libc = dlopen("libc.so", RTLD_LAZY);
        if (!libc) {
            LOGD("Failed to open libc.so");
            return;
        }

        void* property_get_addr = dlsym(libc, "__system_property_get");
        if (!property_get_addr) {
            LOGD("Failed to find __system_property_get");
            dlclose(libc);
            return;
        }

        api->pltHookRegister(".*", "__system_property_get", (void*)hooked_property_get, (void**)&orig_property_get);
        
        // Hook property_list (used by getprop to dump all properties)
        void* property_list_addr = dlsym(libc, "__system_property_foreach");
        if (!property_list_addr) {
            LOGD("Failed to find __system_property_foreach");
        } else {
            api->pltHookRegister(".*", "__system_property_foreach", (void*)hooked_property_list, (void**)&orig_property_list);
        }

        // Commit all hooks
        if (api->pltHookCommit()) {
            LOGD("Hooks committed successfully");
        } else {
            LOGD("Failed to commit hooks");
        }

        dlclose(libc);
    }
};

// Initialize static members
MyZygiskModule::property_get_t MyZygiskModule::orig_property_get = nullptr;
MyZygiskModule::property_list_t MyZygiskModule::orig_property_list = nullptr;

// Register the module
REGISTER_ZYGISK_MODULE(MyZygiskModule)
