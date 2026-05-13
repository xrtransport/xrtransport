// SPDX-License-Identifier: LGPL-3.0-or-later

#include "rpc.h"
#include "function_table.h"
#include "available_extensions.h"
#include "runtime.h"
#include "synchronization.h"
#include "module_loader.h"

#include "xrtransport/serialization/deserializer.h"
#include "xrtransport/extensions/extension_functions.h"
#include "xrtransport/time.h"
#include "xrtransport/api.h"

#include "openxr/openxr_loader_negotiation.h"

#if defined(__ANDROID__)
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#endif

#if defined(__linux__)
#define XR_USE_TIMESPEC
#elif defined(_WIN32)
#define XR_USE_PLATFORM_WIN32
#endif

#include "openxr/openxr_platform.h"
#include "openxr/openxr.h"

#include <spdlog/spdlog.h>

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstddef>

using namespace xrtransport;

// different name to allow static, this symbol must not be exported per spec
static PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr_next; // unused next layer
static XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddrImpl(
    XrInstance                                  instance,
    const char*                                 name,
    PFN_xrVoidFunction*                         function);

static PFN_xrEnumerateInstanceExtensionProperties pfn_xrEnumerateInstanceExtensionProperties_next;
static XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionPropertiesImpl(
    const char*                                 layerName,
    uint32_t                                    propertyCapacityInput,
    uint32_t*                                   propertyCountOutput,
    XrExtensionProperties*                      properties);

static PFN_xrCreateInstance pfn_xrCreateInstance_next;
static XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstanceImpl(
    const XrInstanceCreateInfo*                 createInfo,
    XrInstance*                                 instance);

// built-in extension functions
#ifdef _WIN32
static PFN_xrConvertWin32PerformanceCounterToTimeKHR pfn_xrConvertWin32PerformanceCounterToTimeKHR_next;
static XRAPI_ATTR XrResult XRAPI_CALL xrConvertWin32PerformanceCounterToTimeKHRImpl(
    XrInstance                                  instance,
    const LARGE_INTEGER*                        performanceCounter,
    XrTime*                                     time);

static PFN_xrConvertTimeToWin32PerformanceCounterKHR pfn_xrConvertTimeToWin32PerformanceCounterKHR_next;
static XRAPI_ATTR XrResult XRAPI_CALL xrConvertTimeToWin32PerformanceCounterKHRImpl(
    XrInstance                                  instance,
    XrTime                                      time,
    LARGE_INTEGER*                              performanceCounter);
#else
static PFN_xrConvertTimespecTimeToTimeKHR pfn_xrConvertTimespecTimeToTimeKHR_next;
static XRAPI_ATTR XrResult XRAPI_CALL xrConvertTimespecTimeToTimeKHRImpl(
    XrInstance                                  instance,
    const struct timespec*                      timespecTime,
    XrTime*                                     time);

static PFN_xrConvertTimeToTimespecTimeKHR pfn_xrConvertTimeToTimespecTimeKHR_next;
static XRAPI_ATTR XrResult XRAPI_CALL xrConvertTimeToTimespecTimeKHRImpl(
    XrInstance                                  instance,
    XrTime                                      time,
    struct timespec*                            timespecTime);
#endif

// custom implementation of this to avoid expensive generated RPC call
static PFN_xrPollEvent pfn_xrPollEvent_next;
static XRAPI_ATTR XrResult XRAPI_CALL xrPollEventImpl(
    XrInstance                                  instance,
    XrEventDataBuffer*                          eventData);

// custom xrtransport functions for API layers
static XRAPI_ATTR XrResult XRAPI_CALL xrtransportGetTransport(xrtp_Transport* transport_out);

static std::vector<LoadedModule> modules;
static std::unordered_map<std::string, ExtensionInfo> available_extensions;

static XrInstance saved_instance = XR_NULL_HANDLE;
static std::unordered_set<std::string> available_functions = {
    "xrEnumerateInstanceExtensionProperties",
    "xrEnumerateApiLayerProperties",
    "xrCreateInstance"
};

static XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddrImpl(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!function) {
        return XR_ERROR_HANDLE_INVALID;
    }

    if (name == nullptr) {
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    std::string name_str(name);

    // allow API layers to access transport (allowed with null instance handle)
    if (name_str == "xrtransportGetTransport") {
        *function = reinterpret_cast<PFN_xrVoidFunction>(xrtransportGetTransport);
        return XR_SUCCESS;
    }

    // for spec compliance
    if (instance == XR_NULL_HANDLE &&
        name_str != "xrEnumerateInstanceExtensionProperties" &&
        name_str != "xrEnumerateApiLayerProperties" &&
        name_str != "xrCreateInstance") {
        if (function) *function = nullptr;
        return XR_ERROR_HANDLE_INVALID;
    }

    // check if function is in core or enabled extensions
    if (available_functions.find(name_str) == available_functions.end()) {
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    PFN_xrVoidFunction from_function_table{};
    get_runtime().get_function_table().get_function(name_str, from_function_table);
    if (from_function_table) {
        *function = from_function_table;
        return XR_SUCCESS;
    }
    else {
        // somehow the function was not in either table, but *was* in available_functions.
        // this should never happen, but just return XR_ERROR_FUNCTION_UNSUPPORTED
        spdlog::warn("Function was marked available, but is not in function table");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }
}

static XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionPropertiesImpl(const char* layerName, uint32_t propertyCapacityInput, uint32_t* propertyCountOutput, XrExtensionProperties* properties) {
    // If a layer is specified, just don't return anything
    // TODO: maybe we should expose server extensions to this?
    if (layerName) {
        *propertyCountOutput = 0;
        return XR_SUCCESS;
    }

    uint32_t extension_count = available_extensions.size();

    if (propertyCapacityInput != 0 && propertyCapacityInput < extension_count) {
        *propertyCountOutput = extension_count;
        return XR_ERROR_SIZE_INSUFFICIENT;
    }

    if (propertyCapacityInput == 0) {
        *propertyCountOutput = extension_count;
        return XR_SUCCESS;
    }

    int i = 0;
    for (const auto& [extension_name, extension_info] : available_extensions) {
        XrExtensionProperties& ext_out = properties[i++];
        std::memcpy(ext_out.extensionName, extension_name.c_str(), extension_name.size() + 1);
        ext_out.extensionVersion = extension_info.version;
    }

    return XR_SUCCESS;
}

static XrBaseOutStructure* remove_from_chain(XrBaseOutStructure* base, XrStructureType target_type) {
    XrBaseOutStructure* result = nullptr;

    XrBaseOutStructure* prev_node = base;
    XrBaseOutStructure* node = base->next;
    while (node != nullptr) {
        if (node->type == target_type) {
            // if we haven't already found one of the target type, save it
            if (!result) {
                result = node;
            }
            prev_node->next = node->next;
            node = node->next;
        }
        else {
            prev_node = node;
            node = node->next;
        }
    }

    return result;
}

static XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstanceImpl(const XrInstanceCreateInfo* create_info, XrInstance* instance) {
    if (saved_instance != XR_NULL_HANDLE) {
        // can't create multiple instances
        return XR_ERROR_LIMIT_REACHED;
    }

    if (create_info->applicationInfo.apiVersion > XR_CURRENT_API_VERSION) {
        return XR_ERROR_API_VERSION_UNSUPPORTED;
    }

    // check available extensions and populate available functions

    // first make sure all extensions are available
    for (int i = 0; i < create_info->enabledExtensionCount; i++) {
        std::string extension_name = create_info->enabledExtensionNames[i];
        if (available_extensions.find(extension_name) == available_extensions.end()) {
            return XR_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    // now populate available functions
    for (int i = 0; i < create_info->enabledExtensionCount; i++) {
        std::string extension_name = create_info->enabledExtensionNames[i];

        if (available_extensions.find(extension_name) != available_extensions.end()) {
            for (auto& function_name : available_extensions.at(extension_name).function_names) {
                available_functions.emplace(function_name);
            }
        }
    }

    // populate core functions
    for (auto& function_name : core_functions) {
        available_functions.emplace(function_name);
    }

    // Remove platform structs from pNext chain

    // Technically a violation of the spec to manipulate the chain, but it's easier than copying the whole chain
    // and I don't think anyone relies on their XrInstanceCreateInfo not being edited.
    XrBaseOutStructure* chain_base = reinterpret_cast<XrBaseOutStructure*>(const_cast<XrInstanceCreateInfo*>(create_info));

#ifdef __ANDROID__
    // TODO: It is unclear if we will need to save the contents of this struct
    // either way it needs to be removed from the chain before going to the host
    auto android_create_info = reinterpret_cast<XrInstanceCreateInfoAndroidKHR*>(remove_from_chain(chain_base, XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR));

    // this struct is required on Android
    if (!android_create_info) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
#endif

    // Remove any extensions requested by the application that are provided locally.
    // The server should not be able to see these.

    std::unordered_set<std::string> provided_extensions;
    for (auto& module : modules) {
        auto& module_info = *module.module->get_module_info();
        for (uint32_t i = 0; i < module_info.num_extensions; i++) {
            auto& extension = module_info.extensions[i];
            provided_extensions.emplace(extension.extension_name);
        }
    }
#ifdef __ANDROID__
    provided_extensions.emplace(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
#endif

    std::vector<const char*> requested_extensions(
        create_info->enabledExtensionNames,
        create_info->enabledExtensionNames + create_info->enabledExtensionCount
    );

    for (auto it = requested_extensions.begin(); it != requested_extensions.end();) {
        if (provided_extensions.find(std::string(*it)) != provided_extensions.end()) {
            it = requested_extensions.erase(it);
        }
        else {
            ++it;
        }
    }

    XrInstanceCreateInfo modified_create_info;
    std::memcpy(&modified_create_info, create_info, sizeof(XrInstanceCreateInfo));
    modified_create_info.enabledExtensionCount = static_cast<uint32_t>(requested_extensions.size());
    modified_create_info.enabledExtensionNames = requested_extensions.data();

    // Do the instance creation
    XrResult result = rpc::xrCreateInstance(&modified_create_info, instance);
    if (XR_SUCCEEDED(result)) {
        saved_instance = *instance;

        // now that instance has been created, synchronization can start
        enable_synchronization();

        // run instance callback on all modules
        for (const auto& module : modules) {
            module.module->on_instance(saved_instance, xrGetInstanceProcAddrImpl);
        }
    }
    else {
        // If instance creation failed, no functions are available
        available_functions.clear();
    }
    return result;
}

static void layer_built_in_functions(FunctionTable& function_table) {
    function_table.add_function_layer("xrGetInstanceProcAddr", xrGetInstanceProcAddrImpl, pfn_xrGetInstanceProcAddr_next);
    function_table.add_function_layer("xrEnumerateInstanceExtensionProperties", xrEnumerateInstanceExtensionPropertiesImpl, pfn_xrEnumerateInstanceExtensionProperties_next);
    function_table.add_function_layer("xrCreateInstance", xrCreateInstanceImpl, pfn_xrCreateInstance_next);
    function_table.add_function_layer("xrPollEvent", xrPollEventImpl, pfn_xrPollEvent_next);
#ifdef _WIN32
    function_table.add_function_layer("xrConvertWin32PerformanceCounterToTimeKHR", xrConvertWin32PerformanceCounterToTimeKHRImpl, pfn_xrConvertWin32PerformanceCounterToTimeKHR_next);
    function_table.add_function_layer("xrConvertTimeToWin32PerformanceCounterKHR", xrConvertTimeToWin32PerformanceCounterKHRImpl, pfn_xrConvertTimeToWin32PerformanceCounterKHR_next);
#else
    function_table.add_function_layer("xrConvertTimespecTimeToTimeKHR", xrConvertTimespecTimeToTimeKHRImpl, pfn_xrConvertTimespecTimeToTimeKHR_next);
    function_table.add_function_layer("xrConvertTimeToTimespecTimeKHR", xrConvertTimeToTimespecTimeKHRImpl, pfn_xrConvertTimeToTimespecTimeKHR_next);
#endif
    XrResult (XRAPI_PTR *dummy_next)(xrtp_Transport*);
    function_table.add_function_layer("xrtransportGetTransport", xrtransportGetTransport, dummy_next);
}

static void layer_module_functions(FunctionTable& function_table, const std::vector<LoadedModule>& modules) {
    for (const auto& module : modules) {
        auto& module_info = *module.module->get_module_info();
        for (uint32_t i = 0; i < module_info.num_functions; i++) {
            const auto& function = module_info.functions[i];
            function_table.add_function_layer(function.function_name, function.new_function, *function.old_function);
        }
    }
}

extern "C" XRTP_API_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* runtimeRequest) {
    spdlog::info("xrtransport client loaded");

    if (!loaderInfo ||
        !runtimeRequest ||
        loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
        runtimeRequest->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST ||
        runtimeRequest->structVersion != XR_RUNTIME_INFO_STRUCT_VERSION ||
        runtimeRequest->structSize != sizeof(XrNegotiateRuntimeRequest) ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->minApiVersion > XR_CURRENT_API_VERSION ||
        loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    
    // Initialize function table
    auto& function_table = get_runtime().get_function_table();
    function_table.init_with_rpc_functions();
    layer_built_in_functions(function_table);

    // Load modules
    modules = load_modules(get_runtime().get_transport().get_handle());

    // Collect available extensions
    available_extensions = collect_available_extensions(modules);

    // Layer module functions onto function table
    layer_module_functions(function_table, modules);

    runtimeRequest->getInstanceProcAddr = xrGetInstanceProcAddrImpl;
    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;

    return XR_SUCCESS;
}

static XRAPI_ATTR XrResult XRAPI_CALL xrtransportGetTransport(xrtp_Transport* transport_out) {
    *transport_out = get_runtime().get_transport().get_handle();
    return XR_SUCCESS;
}

#ifdef _WIN32
static XRAPI_ATTR XrResult XRAPI_CALL xrConvertWin32PerformanceCounterToTimeKHRImpl(
    XrInstance                                  instance,
    const LARGE_INTEGER*                        performanceCounter,
    XrTime*                                     time)
{
    if (instance != saved_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }
    convert_from_platform_time(performanceCounter, time);
    return XR_SUCCESS;
}

static XRAPI_ATTR XrResult XRAPI_CALL xrConvertTimeToWin32PerformanceCounterKHRImpl(
    XrInstance                                  instance,
    XrTime                                      time,
    LARGE_INTEGER*                              performanceCounter)
{
    if (instance != saved_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }
    convert_to_platform_time(time, performanceCounter);
    return XR_SUCCESS;
}
#else
static XRAPI_ATTR XrResult XRAPI_CALL xrConvertTimespecTimeToTimeKHRImpl(
    XrInstance                                  instance,
    const struct timespec*                      timespecTime,
    XrTime*                                     time)
{
    if (instance != saved_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }
    convert_from_platform_time(timespecTime, time);
    return XR_SUCCESS;
}

static XRAPI_ATTR XrResult XRAPI_CALL xrConvertTimeToTimespecTimeKHRImpl(
    XrInstance                                  instance,
    XrTime                                      time,
    struct timespec*                            timespecTime)
{
    if (instance != saved_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }
    convert_to_platform_time(time, timespecTime);
    return XR_SUCCESS;
}
#endif

// Custom implementation to avoid the generated RPC call, which sends a lot of data back and
// forth. This one sends nothing and only reads what the runtime returns.
static XRAPI_ATTR XrResult XRAPI_CALL xrPollEventImpl(
    XrInstance                                  instance,
    XrEventDataBuffer*                          event_data_out)
{
    Transport& transport = get_runtime().get_transport();

    auto msg_out = transport.start_message(XRTP_MSG_POLL_EVENT);
    msg_out.flush();

    XrResult result{};
    XrBaseOutStructure* event_data_in{};

    auto msg_in = transport.await_message(XRTP_MSG_POLL_EVENT_RETURN);
    DeserializeContext d_ctx(msg_in.buffer);
    deserialize(&result, d_ctx);
    if (result != XR_SUCCESS) {
        // either XR_EVENT_UNAVAILABLE or an error
        return result;
    }

    deserialize_xr(&event_data_in, d_ctx);

    // copy first event struct onto event_data_out
    size_t first_struct_size = size_lookup(event_data_in->type);
    std::memcpy(event_data_out, event_data_in, first_struct_size);

    // marks the point that we must not write past
    uintptr_t end_of_buffer = reinterpret_cast<uintptr_t>(event_data_out) + sizeof(XrEventDataBuffer);

    // reference to the last struct copied into the output
    XrBaseOutStructure* last_struct_out = reinterpret_cast<XrBaseOutStructure*>(event_data_out);
    size_t last_struct_size = first_struct_size;

    while (last_struct_out->next) {
        // this pointer still points to a dynamically allocated deserialized struct.
        // we will find a spot for it in the output buffer and copy it in.
        XrBaseOutStructure* struct_in = last_struct_out->next;
        size_t struct_size = size_lookup(struct_in->type);

        uintptr_t struct_out_location = reinterpret_cast<uintptr_t>(last_struct_out) + last_struct_size;
        // align the location
        struct_out_location =
            (struct_out_location + alignof(std::max_align_t) - 1) &
            ~(alignof(std::max_align_t) - 1);
        
        if (struct_out_location + struct_size > end_of_buffer) {
            // we will not copy this one because it would overrun the provided XrEventDataBuffer
            spdlog::warn("Server sent too many events from xrPollEvent. First event dropped: {}", (int)struct_in->type);
            break;
        }

        // copy struct into buffer
        XrBaseOutStructure* struct_out = reinterpret_cast<XrBaseOutStructure*>(struct_out_location);
        std::memcpy(struct_out, struct_in, struct_size);

        // make last struct's next pointer point to the copied struct within the buffer
        last_struct_out->next = struct_out;

        last_struct_out = struct_out;
        last_struct_size = struct_size;
    }

    // now that everything is moved into the user-provided buffer, we can clean up the dynamically
    // allocated struct(s)
    cleanup_xr(event_data_in);

    return XR_SUCCESS;
}