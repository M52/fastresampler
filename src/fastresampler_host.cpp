#include <string.h>
#include <stdio.h>
#include <string>
#include "fastresampler_host.h"
#include "fastresampler_host_vst2.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "platform/fastresampler_platform.h"

namespace SV = Steinberg::Vst;

#define HOST_MAX_PARAM_CHANGES 1024

#if SMTG_OS_WINDOWS
    #define HOST_PLATFORM_VIEW_TYPE Steinberg::kPlatformTypeHWND
#elif SMTG_OS_MACOS
    #define HOST_PLATFORM_VIEW_TYPE Steinberg::kPlatformTypeNSView
#else
    #define HOST_PLATFORM_VIEW_TYPE Steinberg::kPlatformTypeX11EmbedWindowID
#endif

struct HostContext {
    SV::HostApplication* app;
};

struct HostCallbacks : public SV::IComponentHandler, public Steinberg::IPlugFrame {
    HostPlugin*  plugin;
    HostResizeFn resize_fn;
    void*        resize_user;
    bool         in_resize;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, SV::IComponentHandler::iid)) {
            *obj = static_cast<SV::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::IPlugFrame::iid)) {
            *obj = static_cast<Steinberg::IPlugFrame*>(this);
            return Steinberg::kResultOk;
        }
        if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<SV::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }

    Steinberg::tresult PLUGIN_API beginEdit(SV::ParamID id) override;
    Steinberg::tresult PLUGIN_API performEdit(SV::ParamID id, SV::ParamValue value) override;
    Steinberg::tresult PLUGIN_API endEdit(SV::ParamID id) override;
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* new_size) override;
};

struct HostPlugin {
    Vst2Plugin*           vst2;

    VST3::Hosting::Module::Ptr module;
    SV::IComponent*       component;
    SV::IAudioProcessor*  processor;
    SV::IEditController*  controller;
    bool                  component_initialized;
    bool                  controller_initialized;
    bool                  controller_is_component;
    SV::ConnectionProxy*  component_cp;
    SV::ConnectionProxy*  controller_cp;

    Steinberg::IPlugView* view;
    bool                  view_attached;
    HostCallbacks         callbacks;

    SV::HostProcessData         process_data;
    SV::EventList               events;
    SV::ParameterChanges        param_changes;
    SV::ParameterChangeTransfer param_transfer;
    SV::ProcessContext          context;

    SV::ParamID cc_param[MIDI_CHANNEL_COUNT][SV::kCountCtrlNumber];

    char   class_uid[FR_STATE_CLASS_UID_SIZE];
    bool   has_class_uid;

    int    main_out_bus;
    int    flat_bus[FR_MAX_RENDER_CHANNELS];
    int    flat_channel[FR_MAX_RENDER_CHANNELS];
    int    flat_count;
    double sample_rate;
    int    block_size;
    bool   offline;
    bool   active;
    bool   processing;
    bool   io_changed;

    HostPluginInfo info;
};

Steinberg::tresult PLUGIN_API HostCallbacks::beginEdit(SV::ParamID) {
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API HostCallbacks::performEdit(SV::ParamID id, SV::ParamValue value) {
    if (plugin) plugin->param_transfer.addChange(id, value, 0);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API HostCallbacks::endEdit(SV::ParamID) {
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API HostCallbacks::restartComponent(Steinberg::int32 flags) {
    fr_log("host: restartComponent(0x%x)", (unsigned)flags);
    if (!plugin) return Steinberg::kResultOk;
    if ((flags & SV::kLatencyChanged) && plugin->processor) {
        plugin->info.latency_frames = (int)plugin->processor->getLatencySamples();
    }
    if (flags & SV::kIoChanged) plugin->io_changed = true;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API HostCallbacks::resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* new_size) {
    if (!view || !new_size || !plugin || view != plugin->view) return Steinberg::kInvalidArgument;
    if (in_resize) return Steinberg::kResultFalse;
    in_resize = true;
    if (resize_fn) resize_fn(resize_user, new_size->getWidth(), new_size->getHeight());
    Steinberg::ViewRect current;
    if (view->getSize(&current) == Steinberg::kResultTrue &&
        (current.getWidth() != new_size->getWidth() || current.getHeight() != new_size->getHeight())) {
        view->onSize(new_size);
    }
    in_resize = false;
    return Steinberg::kResultTrue;
}

const char* host_format_name(int format) {
    return format == HOST_FORMAT_VST2 ? "VSTi" : "VST3";
}

int host_format_of_path(const char* path) {
    return plat_path_has_ext(path, ".dll") ? HOST_FORMAT_VST2 : HOST_FORMAT_VST3;
}

HostContext* host_create(void) {
    HostContext* ctx = new HostContext();
    ctx->app = new SV::HostApplication();
    return ctx;
}

void host_destroy(HostContext* ctx) {
    if (!ctx) return;
    if (ctx->app) ctx->app->release();
    delete ctx;
}

static void host_fail(char* err, size_t err_len, const char* msg) {
    fr_strcpy(err, err_len, msg);
    fr_log("host: %s", msg);
}

static void host_setup_busses(HostPlugin* p) {
    SV::IComponent* c = p->component;
    Steinberg::int32 out_count = c->getBusCount(SV::kAudio, SV::kOutput);
    p->main_out_bus = 0;
    for (Steinberg::int32 i = 0; i < out_count; ++i) {
        SV::BusInfo info = {};
        if (c->getBusInfo(SV::kAudio, SV::kOutput, i, info) == Steinberg::kResultOk && info.busType == SV::kMain) {
            p->main_out_bus = i;
            break;
        }
    }
    p->flat_count = 0;
    for (Steinberg::int32 i = 0; i < out_count; ++i) {
        c->activateBus(SV::kAudio, SV::kOutput, i, true);
        SV::BusInfo info = {};
        if (c->getBusInfo(SV::kAudio, SV::kOutput, i, info) != Steinberg::kResultOk) continue;
        for (Steinberg::int32 ch = 0; ch < info.channelCount; ++ch) {
            if (p->flat_count >= FR_MAX_RENDER_CHANNELS) break;
            p->flat_bus[p->flat_count] = i;
            p->flat_channel[p->flat_count] = (int)ch;
            p->flat_count++;
        }
    }
    Steinberg::int32 in_count = c->getBusCount(SV::kAudio, SV::kInput);
    for (Steinberg::int32 i = 0; i < in_count; ++i) {
        c->activateBus(SV::kAudio, SV::kInput, i, false);
    }
    Steinberg::int32 ev_in = c->getBusCount(SV::kEvent, SV::kInput);
    for (Steinberg::int32 i = 0; i < ev_in; ++i) {
        c->activateBus(SV::kEvent, SV::kInput, i, i == 0);
    }
    Steinberg::int32 ev_out = c->getBusCount(SV::kEvent, SV::kOutput);
    for (Steinberg::int32 i = 0; i < ev_out; ++i) {
        c->activateBus(SV::kEvent, SV::kOutput, i, false);
    }

    SV::BusInfo main_info = {};
    p->info.output_channels = 0;
    if (out_count > 0 && c->getBusInfo(SV::kAudio, SV::kOutput, p->main_out_bus, main_info) == Steinberg::kResultOk) {
        p->info.output_channels = (int)main_info.channelCount;
    }
    p->info.total_output_channels = p->flat_count;
    p->info.output_bus_count = (int)out_count;
}

static void host_read_midi_mapping(HostPlugin* p) {
    for (int ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
        for (int ctrl = 0; ctrl < SV::kCountCtrlNumber; ++ctrl) p->cc_param[ch][ctrl] = SV::kNoParamId;
    }
    p->info.has_midi_mapping = false;
    if (!p->controller) return;
    Steinberg::FUnknownPtr<SV::IMidiMapping> mapping(p->controller);
    if (!mapping) return;
    int mapped = 0;
    for (int ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
        for (int ctrl = 0; ctrl < SV::kCountCtrlNumber; ++ctrl) {
            SV::ParamID pid = SV::kNoParamId;
            if (mapping->getMidiControllerAssignment(0, (Steinberg::int16)ch, (SV::CtrlNumber)ctrl, pid) == Steinberg::kResultOk) {
                p->cc_param[ch][ctrl] = pid;
                mapped++;
            }
        }
    }
    p->info.has_midi_mapping = mapped > 0;
    fr_log("host: %d MIDI controller assignments", mapped);
}

static void host_sync_controller_state(HostPlugin* p) {
    if (!p->controller || p->controller_is_component) return;
    Steinberg::MemoryStream stream;
    if (p->component->getState(&stream) != Steinberg::kResultOk) return;
    Steinberg::int64 pos = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, &pos);
    p->controller->setComponentState(&stream);
}

static bool host_pick_class(const VST3::Hosting::PluginFactory& factory, VST3::Hosting::ClassInfo* out) {
    bool found = false;
    bool found_instrument = false;
    for (const VST3::Hosting::ClassInfo& ci : factory.classInfos()) {
        if (ci.category() != kVstAudioEffectClass) continue;
        bool instrument = ci.subCategoriesString().find(SV::PlugType::kInstrument) != std::string::npos;
        if (!found || (instrument && !found_instrument)) {
            *out = ci;
            found = true;
            found_instrument = instrument;
        }
    }
    return found;
}

HostPlugin* host_plugin_load(HostContext* ctx, const char* path, double sample_rate,
                             int block_size, char* err, size_t err_len) {
    err[0] = '\0';
    if (host_format_of_path(path) == HOST_FORMAT_VST2) {
        HostPlugin* p = new HostPlugin();
        p->vst2 = vst2_plugin_load(path, sample_rate, block_size, &p->info, err, err_len);
        if (!p->vst2) {
            delete p;
            return nullptr;
        }
        return p;
    }
    std::string module_error;
    VST3::Hosting::Module::Ptr module = VST3::Hosting::Module::create(path, module_error);
    if (!module) {
        snprintf(err, err_len, "Could not load the module: %s", module_error.c_str());
        fr_log("host: %s (%s)", err, path);
        return nullptr;
    }

    HostPlugin* p = new HostPlugin();
    p->module = module;
    p->callbacks.plugin = p;
    p->main_out_bus = 0;

    const VST3::Hosting::PluginFactory& factory = module->getFactory();
    factory.setHostContext(ctx->app);

    VST3::Hosting::ClassInfo chosen;
    if (!host_pick_class(factory, &chosen)) {
        host_fail(err, err_len, "The module has no audio module class");
        host_plugin_unload(p);
        return nullptr;
    }
    memcpy(p->class_uid, chosen.ID().data(), sizeof(p->class_uid));
    p->has_class_uid = true;
    fr_strcpy(p->info.name, sizeof(p->info.name), chosen.name().c_str());
    fr_strcpy(p->info.vendor, sizeof(p->info.vendor), chosen.vendor().c_str());
    fr_strcpy(p->info.version, sizeof(p->info.version), chosen.version().c_str());
    fr_strcpy(p->info.category, sizeof(p->info.category), chosen.subCategoriesString().c_str());

    p->component = factory.createInstance<SV::IComponent>(chosen.ID()).take();
    if (!p->component) {
        host_fail(err, err_len, "Could not create the component");
        host_plugin_unload(p);
        return nullptr;
    }
    if (p->component->initialize(ctx->app) != Steinberg::kResultOk) {
        host_fail(err, err_len, "The component did not initialize");
        host_plugin_unload(p);
        return nullptr;
    }
    p->component_initialized = true;

    if (p->component->queryInterface(SV::IEditController::iid, (void**)&p->controller) == Steinberg::kResultOk && p->controller) {
        p->controller_is_component = true;
    } else {
        p->controller = nullptr;
        Steinberg::TUID controller_cid = {};
        if (p->component->getControllerClassId(controller_cid) == Steinberg::kResultOk) {
            p->controller = factory.createInstance<SV::IEditController>(VST3::UID(controller_cid)).take();
            if (p->controller && p->controller->initialize(ctx->app) != Steinberg::kResultOk) {
                fr_log("host: the controller did not initialize, running without one");
                p->controller->release();
                p->controller = nullptr;
            }
            if (p->controller) p->controller_initialized = true;
        }
    }
    if (p->controller && !p->controller_is_component) {
        Steinberg::FUnknownPtr<SV::IConnectionPoint> comp_cp(p->component);
        Steinberg::FUnknownPtr<SV::IConnectionPoint> ctrl_cp(p->controller);
        if (comp_cp && ctrl_cp) {
            p->component_cp = new SV::ConnectionProxy(comp_cp);
            p->controller_cp = new SV::ConnectionProxy(ctrl_cp);
            if (p->component_cp->connect(ctrl_cp) != Steinberg::kResultTrue ||
                p->controller_cp->connect(comp_cp) != Steinberg::kResultTrue) {
                fr_log("host: component and controller did not connect");
            }
        }
    }
    p->info.format = HOST_FORMAT_VST3;
    p->info.has_controller = p->controller != nullptr;
    if (p->controller) {
        p->controller->setComponentHandler(&p->callbacks);
        host_sync_controller_state(p);
    }

    if (p->component->queryInterface(SV::IAudioProcessor::iid, (void**)&p->processor) != Steinberg::kResultOk || !p->processor) {
        p->processor = nullptr;
        host_fail(err, err_len, "The component has no audio processor");
        host_plugin_unload(p);
        return nullptr;
    }
    if (p->processor->canProcessSampleSize(SV::kSample32) != Steinberg::kResultOk) {
        host_fail(err, err_len, "The plugin does not process 32 bit float");
        host_plugin_unload(p);
        return nullptr;
    }

    host_setup_busses(p);
    if (p->component->getBusCount(SV::kAudio, SV::kOutput) == 0) {
        host_fail(err, err_len, "The plugin has no audio output");
        host_plugin_unload(p);
        return nullptr;
    }
    host_read_midi_mapping(p);

    p->events.setMaxSize(FR_MIDI_QUEUE_CAPACITY);
    p->param_changes.setMaxParameters(HOST_MAX_PARAM_CHANGES);
    p->param_transfer.setMaxParameters(HOST_MAX_PARAM_CHANGES);
    memset(&p->context, 0, sizeof(p->context));
    p->context.state = SV::ProcessContext::kPlaying | SV::ProcessContext::kTempoValid |
                       SV::ProcessContext::kTimeSigValid | SV::ProcessContext::kContTimeValid |
                       SV::ProcessContext::kProjectTimeMusicValid;
    p->context.tempo = FR_DEFAULT_TEMPO;
    p->context.timeSigNumerator = 4;
    p->context.timeSigDenominator = 4;
    p->process_data.inputEvents = &p->events;
    p->process_data.inputParameterChanges = &p->param_changes;
    p->process_data.processContext = &p->context;
    p->process_data.outputParameterChanges = nullptr;
    p->process_data.outputEvents = nullptr;

    if (!host_plugin_setup_processing(p, sample_rate, block_size, false)) {
        host_fail(err, err_len, "The plugin refused the processing setup");
        host_plugin_unload(p);
        return nullptr;
    }

    fr_log("host: loaded '%s' by %s (%s), %d output channels on the main bus, %d over %d busses, latency %d",
           p->info.name, p->info.vendor, p->info.category, p->info.output_channels,
           p->info.total_output_channels, p->info.output_bus_count, p->info.latency_frames);
    return p;
}

void host_plugin_unload(HostPlugin* p) {
    if (!p) return;
    if (p->vst2) {
        vst2_plugin_unload(p->vst2);
        delete p;
        return;
    }
    host_plugin_editor_close(p);

    if (p->processor && p->processing) p->processor->setProcessing(false);
    p->processing = false;
    if (p->component && p->active) p->component->setActive(false);
    p->active = false;

    if (p->component_cp) {
        p->component_cp->disconnect();
        p->component_cp->release();
        p->component_cp = nullptr;
    }
    if (p->controller_cp) {
        p->controller_cp->disconnect();
        p->controller_cp->release();
        p->controller_cp = nullptr;
    }
    if (p->controller && p->controller_initialized) p->controller->terminate();
    if (p->component && p->component_initialized) p->component->terminate();

    if (p->processor) p->processor->release();
    if (p->controller) p->controller->release();
    if (p->component) p->component->release();
    p->processor = nullptr;
    p->controller = nullptr;
    p->component = nullptr;

    p->process_data.unprepare();
    p->module.reset();
    delete p;
}

bool host_plugin_setup_processing(HostPlugin* p, double sample_rate, int block_size, bool offline) {
    if (p->vst2) return vst2_plugin_setup_processing(p->vst2, sample_rate, block_size, offline, &p->info);
    if (!p->processor || !p->component) return false;
    if (p->processing) {
        p->processor->setProcessing(false);
        p->processing = false;
    }
    if (p->active) {
        p->component->setActive(false);
        p->active = false;
    }
    p->sample_rate = sample_rate;
    p->block_size = block_size;
    p->offline = offline;
    p->context.sampleRate = sample_rate;

    host_setup_busses(p);

    if (!p->process_data.prepare(*p->component, block_size, SV::kSample32)) {
        fr_log("host: HostProcessData::prepare failed");
        return false;
    }
    SV::ProcessSetup setup = {};
    setup.processMode = offline ? SV::kOffline : SV::kRealtime;
    setup.symbolicSampleSize = SV::kSample32;
    setup.maxSamplesPerBlock = block_size;
    setup.sampleRate = sample_rate;
    if (p->processor->setupProcessing(setup) != Steinberg::kResultOk) {
        fr_log("host: setupProcessing(%.0f Hz, %d) refused", sample_rate, block_size);
        return false;
    }
    if (p->component->setActive(true) != Steinberg::kResultOk) {
        fr_log("host: setActive(true) refused");
        return false;
    }
    p->active = true;
    p->processor->setProcessing(true);
    p->processing = true;
    p->info.latency_frames = (int)p->processor->getLatencySamples();
    return true;
}

const HostPluginInfo* host_plugin_info(const HostPlugin* p) {
    return &p->info;
}

static void host_add_stream_part(HostPlugin* p, StateBuffer* out, int kind, bool from_controller) {
    Steinberg::MemoryStream stream;
    Steinberg::tresult ok = from_controller ? p->controller->getState(&stream)
                                            : p->component->getState(&stream);
    if (ok != Steinberg::kResultOk) return;
    Steinberg::int64 size = stream.getSize();
    if (size <= 0) return;
    state_add_part(out, kind, stream.getData(), (size_t)size);
}

static bool host_stream_from_part(Steinberg::MemoryStream* stream, const void* bytes, size_t size) {
    Steinberg::int32 written = 0;
    if (stream->write((void*)bytes, (Steinberg::int32)size, &written) != Steinberg::kResultOk) return false;
    if ((size_t)written != size) return false;
    Steinberg::int64 pos = 0;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, &pos);
    return true;
}

bool host_plugin_get_state(HostPlugin* p, StateBuffer* out) {
    if (!p) return false;
    if (p->vst2) return vst2_plugin_get_state(p->vst2, out);
    if (!p->component) return false;
    if (!state_begin(out, HOST_FORMAT_VST3, 0)) return false;
    host_add_stream_part(p, out, STATE_PART_VST3_COMPONENT, false);
    if (p->controller) host_add_stream_part(p, out, STATE_PART_VST3_CONTROLLER, true);
    if (p->has_class_uid && state_part_count(out) > 0) {
        state_add_part(out, STATE_PART_VST3_CLASS_UID, p->class_uid, sizeof(p->class_uid));
    }
    if (state_part_count(out) == 0) {
        state_buffer_release(out);
        fr_log("host: '%s' wrote no state", p->info.name);
        return false;
    }
    fr_log("host: read %d bytes of state from '%s'", (int)out->size, p->info.name);
    return true;
}

bool host_plugin_set_state(HostPlugin* p, const void* data, size_t size, char* err, size_t err_len) {
    err[0] = '\0';
    if (!p) return false;
    if (p->vst2) return vst2_plugin_set_state(p->vst2, data, size, &p->info, err, err_len);
    int format = HOST_FORMAT_VST3;
    uint32_t unique_id = 0;
    if (!state_read_header(data, size, &format, &unique_id)) {
        fr_strcpy(err, err_len, "The saved plugin state is not readable.");
        return false;
    }
    if (format != HOST_FORMAT_VST3) {
        fr_strcpy(err, err_len, "The saved plugin state was written for a VST 2 instrument.");
        return false;
    }
    if (!p->component) return false;

    const uint8_t* uid = NULL;
    size_t uid_size = 0;
    if (p->has_class_uid && state_find_part(data, size, STATE_PART_VST3_CLASS_UID, &uid, &uid_size)) {
        if (uid_size != sizeof(p->class_uid) || memcmp(uid, p->class_uid, uid_size) != 0) {
            snprintf(err, err_len, "The saved state belongs to another plugin, not %s.", p->info.name);
            fr_log("host: the state's class is not '%s'", p->info.name);
            return false;
        }
    }

    const uint8_t* bytes = NULL;
    size_t bytes_size = 0;
    if (!state_find_part(data, size, STATE_PART_VST3_COMPONENT, &bytes, &bytes_size)) {
        fr_strcpy(err, err_len, "The saved plugin state holds no processor state.");
        return false;
    }

    if (p->processing) {
        p->processor->setProcessing(false);
        p->processing = false;
    }
    if (p->active) {
        p->component->setActive(false);
        p->active = false;
    }

    Steinberg::MemoryStream component_stream;
    if (!host_stream_from_part(&component_stream, bytes, bytes_size)) {
        fr_strcpy(err, err_len, "Out of memory for the saved plugin state.");
        return false;
    }
    bool ok = p->component->setState(&component_stream) == Steinberg::kResultOk;
    if (!ok) fr_log("host: the component refused its saved state");

    if (p->controller && !p->controller_is_component) {
        Steinberg::int64 pos = 0;
        component_stream.seek(0, Steinberg::IBStream::kIBSeekSet, &pos);
        p->controller->setComponentState(&component_stream);
    }
    if (p->controller) {
        const uint8_t* ctrl_bytes = NULL;
        size_t ctrl_size = 0;
        if (state_find_part(data, size, STATE_PART_VST3_CONTROLLER, &ctrl_bytes, &ctrl_size)) {
            Steinberg::MemoryStream controller_stream;
            if (host_stream_from_part(&controller_stream, ctrl_bytes, ctrl_size)) {
                p->controller->setState(&controller_stream);
            }
        }
    }

    p->io_changed = false;
    host_read_midi_mapping(p);
    if (!host_plugin_setup_processing(p, p->sample_rate, p->block_size, p->offline)) {
        fr_strcpy(err, err_len, "The plugin refused to start again with its saved state.");
        return false;
    }
    if (!ok) {
        fr_strcpy(err, err_len, "The plugin did not take its saved state.");
        return false;
    }
    fr_log("host: '%s' took %d bytes of state, %d output channels over %d busses",
           p->info.name, (int)size, p->info.total_output_channels, p->info.output_bus_count);
    return true;
}

bool host_plugin_take_io_change(HostPlugin* p) {
    if (!p) return false;
    if (p->vst2) return vst2_plugin_take_io_change(p->vst2);
    bool changed = p->io_changed;
    p->io_changed = false;
    return changed;
}

static void host_queue_param(HostPlugin* p, int channel, int ctrl, double value, int sample_offset) {
    if (ctrl < 0 || ctrl >= SV::kCountCtrlNumber) return;
    SV::ParamID pid = p->cc_param[channel & 0x0F][ctrl];
    if (pid == SV::kNoParamId) return;
    Steinberg::int32 queue_index = 0;
    SV::IParamValueQueue* queue = p->param_changes.addParameterData(pid, queue_index);
    if (!queue) return;
    Steinberg::int32 point_index = 0;
    queue->addPoint(sample_offset, value, point_index);
}

static void host_translate_midi(HostPlugin* p, const MidiEvent& m, int sample_offset) {
    SV::Event e = {};
    e.busIndex = 0;
    e.sampleOffset = sample_offset;
    e.flags = SV::Event::kIsLive;
    switch (m.status) {
        case MIDI_STATUS_NOTE_ON:
            if (m.data1 == 0) {
                e.type = SV::Event::kNoteOffEvent;
                e.noteOff.channel = m.channel;
                e.noteOff.pitch = m.data0;
                e.noteOff.velocity = 0.0f;
                e.noteOff.noteId = -1;
            } else {
                e.type = SV::Event::kNoteOnEvent;
                e.noteOn.channel = m.channel;
                e.noteOn.pitch = m.data0;
                e.noteOn.velocity = (float)m.data1 / (float)MIDI_VALUE_MAX;
                e.noteOn.noteId = -1;
            }
            p->events.addEvent(e);
            break;
        case MIDI_STATUS_NOTE_OFF:
            e.type = SV::Event::kNoteOffEvent;
            e.noteOff.channel = m.channel;
            e.noteOff.pitch = m.data0;
            e.noteOff.velocity = (float)m.data1 / (float)MIDI_VALUE_MAX;
            e.noteOff.noteId = -1;
            p->events.addEvent(e);
            break;
        case MIDI_STATUS_POLY_PRESSURE:
            e.type = SV::Event::kPolyPressureEvent;
            e.polyPressure.channel = m.channel;
            e.polyPressure.pitch = m.data0;
            e.polyPressure.pressure = (float)m.data1 / (float)MIDI_VALUE_MAX;
            e.polyPressure.noteId = -1;
            p->events.addEvent(e);
            break;
        case MIDI_STATUS_CONTROL_CHANGE:
            host_queue_param(p, m.channel, m.data0, (double)m.data1 / (double)MIDI_VALUE_MAX, sample_offset);
            break;
        case MIDI_STATUS_PITCH_BEND: {
            int value = (m.data0 & 0x7F) | ((m.data1 & 0x7F) << 7);
            host_queue_param(p, m.channel, SV::kPitchBend, (double)value / (double)MIDI_PITCH_BEND_MAX, sample_offset);
            break;
        }
        case MIDI_STATUS_CHANNEL_PRESSURE:
            host_queue_param(p, m.channel, SV::kAfterTouch, (double)m.data0 / (double)MIDI_VALUE_MAX, sample_offset);
            break;
        default:
            break;
    }
}

static void host_run_block(HostPlugin* p, float* const* outputs, int channel_count, int frames);

void host_plugin_process(HostPlugin* p, MidiQueue* midi, float* const* outputs,
                         int channel_count, int frames) {
    if (p->vst2) {
        vst2_plugin_process(p->vst2, midi, outputs, channel_count, frames);
        return;
    }
    if (frames > p->block_size) frames = p->block_size;
    MidiEvent m;
    while (midi_queue_pop(midi, &m)) host_translate_midi(p, m, 0);
    host_run_block(p, outputs, channel_count, frames);
}

void host_plugin_process_events(HostPlugin* p, const MidiEvent* events, const int* sample_offsets,
                                int count, float* const* outputs, int channel_count, int frames) {
    if (p->vst2) {
        vst2_plugin_process_events(p->vst2, events, sample_offsets, count, outputs, channel_count, frames);
        return;
    }
    if (frames > p->block_size) frames = p->block_size;
    for (int i = 0; i < count; ++i) {
        int offset = sample_offsets[i];
        if (offset < 0) offset = 0;
        if (offset >= frames) offset = frames - 1;
        host_translate_midi(p, events[i], offset);
    }
    host_run_block(p, outputs, channel_count, frames);
}

static void host_run_block(HostPlugin* p, float* const* outputs, int channel_count, int frames) {
    p->param_transfer.transferChangesTo(p->param_changes);

    for (Steinberg::int32 b = 0; b < p->process_data.numOutputs; ++b) {
        SV::AudioBusBuffers& bus = p->process_data.outputs[b];
        bus.silenceFlags = 0;
        for (Steinberg::int32 c = 0; c < bus.numChannels; ++c) {
            if (bus.channelBuffers32[c]) memset(bus.channelBuffers32[c], 0, (size_t)frames * sizeof(float));
        }
    }
    for (Steinberg::int32 b = 0; b < p->process_data.numInputs; ++b) {
        SV::AudioBusBuffers& bus = p->process_data.inputs[b];
        bus.silenceFlags = SV::HostProcessData::kAllChannelsSilent;
        for (Steinberg::int32 c = 0; c < bus.numChannels; ++c) {
            if (bus.channelBuffers32[c]) memset(bus.channelBuffers32[c], 0, (size_t)frames * sizeof(float));
        }
    }

    p->process_data.numSamples = frames;
    Steinberg::tresult result = p->processor->process(p->process_data);

    if (result == Steinberg::kResultOk) {
        for (int c = 0; c < channel_count; ++c) {
            int flat = c;
            if (flat >= p->flat_count) {
                if (flat != 1 || p->flat_count != 1) continue;
                flat = 0;
            }
            Steinberg::int32 b = (Steinberg::int32)p->flat_bus[flat];
            if (b >= p->process_data.numOutputs) continue;
            SV::AudioBusBuffers& bus = p->process_data.outputs[b];
            Steinberg::int32 src = (Steinberg::int32)p->flat_channel[flat];
            if (src >= bus.numChannels || !bus.channelBuffers32[src]) continue;
            memcpy(outputs[c], bus.channelBuffers32[src], (size_t)frames * sizeof(float));
        }
    }

    p->context.projectTimeSamples += frames;
    p->context.continousTimeSamples += frames;
    p->context.projectTimeMusic = (double)p->context.projectTimeSamples / p->sample_rate * (p->context.tempo / 60.0);

    p->events.clear();
    p->param_changes.clearQueue();
}

bool host_plugin_editor_create(HostPlugin* p, float content_scale, int* width, int* height) {
    *width = 0;
    *height = 0;
    if (p->vst2) return vst2_plugin_editor_create(p->vst2, width, height);
    if (!p->controller) return false;
    host_plugin_editor_close(p);

    p->view = p->controller->createView(SV::ViewType::kEditor);
    if (!p->view) {
        fr_log("host: the controller has no editor view");
        return false;
    }
    if (p->view->isPlatformTypeSupported(HOST_PLATFORM_VIEW_TYPE) != Steinberg::kResultTrue) {
        fr_log("host: the editor does not support this platform's window type");
        p->view->release();
        p->view = nullptr;
        return false;
    }
    host_plugin_editor_set_scale(p, content_scale);

    Steinberg::ViewRect rect;
    if (p->view->getSize(&rect) == Steinberg::kResultTrue) {
        *width = rect.getWidth();
        *height = rect.getHeight();
    }
    return true;
}

bool host_plugin_editor_attach(HostPlugin* p, void* parent_window, HostResizeFn resize_fn, void* user) {
    if (p->vst2) return vst2_plugin_editor_attach(p->vst2, parent_window, resize_fn, user);
    if (!p->view) return false;
    p->callbacks.resize_fn = resize_fn;
    p->callbacks.resize_user = user;
    p->view->setFrame(&p->callbacks);
    if (p->view->attached(parent_window, HOST_PLATFORM_VIEW_TYPE) != Steinberg::kResultTrue) {
        fr_log("host: IPlugView::attached failed");
        p->view->setFrame(nullptr);
        p->view->release();
        p->view = nullptr;
        return false;
    }
    p->view_attached = true;
    return true;
}

void host_plugin_editor_close(HostPlugin* p) {
    if (p->vst2) {
        vst2_plugin_editor_close(p->vst2);
        return;
    }
    if (!p->view) return;
    p->view->setFrame(nullptr);
    if (p->view_attached) p->view->removed();
    p->view_attached = false;
    p->view->release();
    p->view = nullptr;
    p->callbacks.resize_fn = nullptr;
    p->callbacks.resize_user = nullptr;
}

void host_plugin_editor_idle(HostPlugin* p) {
    if (p->vst2) vst2_plugin_editor_idle(p->vst2);
}

void host_plugin_editor_set_scale(HostPlugin* p, float content_scale) {
    if (!p->view) return;
    Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport> css(p->view);
    if (css) css->setContentScaleFactor(content_scale);
}

void host_plugin_editor_fit(HostPlugin* p, int avail_width, int avail_height, int* width, int* height) {
    if (p->vst2) {
        vst2_plugin_editor_size(p->vst2, width, height);
        return;
    }
    Steinberg::ViewRect current;
    if (!p->view || p->view->getSize(&current) != Steinberg::kResultTrue) return;
    *width = current.getWidth();
    *height = current.getHeight();
    if (p->view->canResize() != Steinberg::kResultTrue) return;
    if (avail_width <= 0 || avail_height <= 0) return;

    Steinberg::ViewRect want(0, 0, avail_width, avail_height);
    p->view->checkSizeConstraint(&want);
    if (want.getWidth() <= 0 || want.getHeight() <= 0) return;
    if (want.getWidth() == current.getWidth() && want.getHeight() == current.getHeight()) return;

    p->callbacks.in_resize = true;
    p->view->onSize(&want);
    p->callbacks.in_resize = false;
    if (p->view->getSize(&current) == Steinberg::kResultTrue) {
        *width = current.getWidth();
        *height = current.getHeight();
    }
}
