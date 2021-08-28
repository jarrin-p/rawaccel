#pragma once

#include "interop-exception.h"

#include <rawaccel-io.hpp>
#include <rawaccel-validate.hpp>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;
using namespace System::Reflection;
using namespace System::Runtime::Serialization;
using namespace Newtonsoft::Json;
using namespace Newtonsoft::Json::Linq;

namespace ra = rawaccel;

ra::driver_settings default_driver_settings;
ra::device_settings default_device_settings;

public ref struct VersionHelper
{
    literal String^ VersionString = RA_VER_STRING;

    static Version^ ValidOrThrow()
    {
        try {
            ra::version_t v = ra::valid_version_or_throw();
            return gcnew Version(v.major, v.minor, v.patch, 0);
        }
        catch (const ra::error& e) {
            throw gcnew InteropException(e);
        }
    }
};

[JsonConverter(Converters::StringEnumConverter::typeid)]
public enum class AccelMode
{
    classic, jump, natural, motivity, power, lut, noaccel
};

[JsonConverter(Converters::StringEnumConverter::typeid)]
public enum class ClassicCapMode {
    in_out, input, output
};

generic <typename T>
[StructLayout(LayoutKind::Sequential)]
public value struct Vec2
{
    T x;
    T y;
};

[StructLayout(LayoutKind::Sequential)]
public value struct AccelArgs
{
    literal int MaxLutPoints = ra::LUT_POINTS_CAPACITY;

    AccelMode mode;

    [JsonProperty("Gain / Velocity")]
    [MarshalAs(UnmanagedType::U1)]
    bool gain;

    double offset;
    double acceleration;
    double decayRate;
    double growthRate;
    double motivity;
    double exponentClassic;
    double scale;
    double weight;
    double exponentPower;
    double limit;
    double midpoint;
    double smooth;

    [JsonProperty("Cap / Jump")]
    Vec2<double> cap;

    [JsonProperty("Cap mode")]
    ClassicCapMode capMode;

    [JsonIgnore]
    int length;

    [MarshalAs(UnmanagedType::ByValArray, SizeConst = ra::LUT_RAW_DATA_CAPACITY)]
    array<float>^ data;

    [OnDeserialized]
    void OnDeserializedMethod(StreamingContext context)
    {
        // data->Length must match SizeConst when marshalling
        length = data->Length;
        array<float>::Resize(data, ra::LUT_RAW_DATA_CAPACITY);
    }
};

[JsonObject(ItemRequired = Required::Always)]
[StructLayout(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public ref struct Profile
{
    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    System::String^ name;

    [JsonProperty("Whole/combined accel (set false for 'by component' mode)")]
    [MarshalAs(UnmanagedType::U1)]
    bool combineMagnitudes;

    double lpNorm;

    [JsonProperty("Stretches domain for horizontal vs vertical inputs")]
    Vec2<double> domainXY;
    [JsonProperty("Stretches accel range for horizontal vs vertical inputs")]
    Vec2<double> rangeXY;

    [JsonProperty("Sensitivity multiplier")]
    double sensitivity;

    [JsonProperty("Y/X sensitivity ratio (vertical sens multiplier)")]
    double yxSensRatio;

    [JsonProperty("Whole or horizontal accel parameters")]
    AccelArgs argsX;
    [JsonProperty("Vertical accel parameters")]
    AccelArgs argsY;

    [JsonIgnore]
    double minimumSpeed;
    [JsonProperty("Input Speed Cap")]
    double maximumSpeed;

    [JsonProperty("Negative directional multipliers")]
    Vec2<double> directionalMultipliers;

    [JsonProperty("Degrees of rotation")]
    double rotation;

    [JsonProperty("Degrees of angle snapping")]
    double snap;

    Profile(ra::profile& args)
    {
        Marshal::PtrToStructure(IntPtr(&args), this);
    }

    Profile() :
        Profile(default_driver_settings.prof) {}
};

[JsonObject(ItemRequired = Required::Always)]
[StructLayout(LayoutKind::Sequential)]
public value struct DeviceConfig {
    [MarshalAs(UnmanagedType::U1)]
    bool disable;

    [MarshalAs(UnmanagedType::U1)]
    [JsonProperty(Required = Required::Default)]
    bool setExtraInfo;

    [JsonProperty("DPI (normalizes sens to 1000dpi and converts input speed unit: counts/ms -> in/s)")]
    int dpi;

    [JsonProperty("Polling rate Hz (keep at 0 for automatic adjustment)")]
    int pollingRate;
    
    [ComponentModel::DefaultValue(ra::DEFAULT_TIME_MIN)]
    [JsonProperty(Required = Required::Default)]
    double minimumTime;

    [ComponentModel::DefaultValue(ra::DEFAULT_TIME_MAX)]
    [JsonProperty(Required = Required::Default)]
    double maximumTime;

    bool ShouldSerializesetExtraInfo()
    {
        return setExtraInfo == true;
    }

    bool ShouldSerializeminimumTime()
    {
        return minimumTime != ra::DEFAULT_TIME_MIN;
    }

    bool ShouldSerializemaximumTime()
    {
        return maximumTime != ra::DEFAULT_TIME_MAX;
    }

    void Init(const ra::device_config& cfg) 
    {
        disable = cfg.disable;
        setExtraInfo = cfg.set_extra_info;
        dpi = cfg.dpi;
        pollingRate = cfg.polling_rate;
        minimumTime = cfg.clamp.min;
        maximumTime = cfg.clamp.max;
    }
};

[JsonObject(ItemRequired = Required::Always)]
[StructLayout(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public ref struct DeviceSettings
{
    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    String^ name;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_NAME_LEN)]
    String^ profile;

    [MarshalAs(UnmanagedType::ByValTStr, SizeConst = ra::MAX_DEV_ID_LEN)]
    String^ id;

    DeviceConfig config;

    DeviceSettings(ra::device_settings& args)
    {
        Marshal::PtrToStructure(IntPtr(&args), this);
    }

    DeviceSettings() :
        DeviceSettings(default_device_settings) {}
};


public ref class ProfileErrors
{
    List<String^>^ tmp;
    bool single;

    delegate void MsgHandler(const char*);

    void Add(const char* msg)
    {
        tmp->Add(gcnew String(msg));
    }

public:
    ref struct SingleProfileErrors
    {
        Profile^ prof;
        array<String^>^ messages;
        int lastX;
        int lastY;
    };

    List<SingleProfileErrors^>^ list;

    ProfileErrors(List<Profile^>^ profiles)
    {
        single = profiles->Count == 1;
        list = gcnew List<SingleProfileErrors^>();
        tmp = gcnew List<String^>();
        MsgHandler^ del = gcnew MsgHandler(this, &ProfileErrors::Add);
        GCHandle gch = GCHandle::Alloc(del);
        auto fp = static_cast<void (*)(const char*)>(
            Marshal::GetFunctionPointerForDelegate(del).ToPointer());
        ra::profile* native_ptr = new ra::profile();

        for each (auto prof in profiles) {
            Marshal::StructureToPtr(prof, IntPtr(native_ptr), false);
            auto [last_x, last_y, _] = ra::valid(*native_ptr, fp);

            if (tmp->Count != 0) {
                auto singleErrors = gcnew SingleProfileErrors();
                singleErrors->messages = tmp->ToArray();
                singleErrors->lastX = last_x;
                singleErrors->lastY = last_y;
                singleErrors->prof = prof;
                list->Add(singleErrors);
                tmp->Clear();
            }
        }

        tmp = nullptr;
        gch.Free();
        delete native_ptr;
    }

    bool Empty()
    {
        return list->Count == 0;
    }

    virtual String^ ToString() override
    {
        Text::StringBuilder^ sb = gcnew Text::StringBuilder();

        for each (auto elem in list) {
            if (!single) {
                sb->AppendFormat("profile: {0}\n", elem->prof->name);
            }

            auto msgs = elem->messages;
            if (elem->prof->combineMagnitudes) {
                for (int i = 0; i < elem->lastX; i++) {
                    sb->AppendFormat("\t{0}\n", msgs[i]);
                }
            }
            else {
                for (int i = 0; i < elem->lastX; i++) {
                    sb->AppendFormat("\tx: {0}\n", msgs[i]);
                }
                for (int i = elem->lastX; i < elem->lastY; i++) {
                    sb->AppendFormat("\ty: {0}\n", msgs[i]);
                }
            }

            for (int i = elem->lastY; i < msgs->Length; i++) {
                sb->AppendFormat("\t{0}\n", msgs[i]);
            }
        }

        return sb->ToString();
    }
};

public ref class DeviceErrors
{
    List<String^>^ tmp;
    bool single;

    delegate void MsgHandler(const char*);

    void Add(const char* msg)
    {
        tmp->Add(gcnew String(msg));
    }

public:
    ref struct SingleDeviceErrors
    {
        DeviceSettings^ settings;
        array<String^>^ messages;
    };

    List<SingleDeviceErrors^>^ list;

    DeviceErrors(List<DeviceSettings^>^ devSettings)
    {
        single = devSettings->Count == 1;
        list = gcnew List<SingleDeviceErrors^>();
        tmp = gcnew List<String^>();
        MsgHandler^ del = gcnew MsgHandler(this, &DeviceErrors::Add);
        GCHandle gch = GCHandle::Alloc(del);
        auto fp = static_cast<void (*)(const char*)>(
            Marshal::GetFunctionPointerForDelegate(del).ToPointer());
        ra::device_settings* native_ptr = new ra::device_settings();

        for each (auto dev in devSettings) {
            Marshal::StructureToPtr(dev, IntPtr(native_ptr), false);
            ra::valid(*native_ptr, fp);

            if (tmp->Count != 0) {
                auto singleErrors = gcnew SingleDeviceErrors();
                singleErrors->messages = tmp->ToArray();
                singleErrors->settings = dev;
                list->Add(singleErrors);
                tmp->Clear();
            }
        }

        tmp = nullptr;
        gch.Free();
        delete native_ptr;
    }

    bool Empty()
    {
        return list->Count == 0;
    }

    virtual String^ ToString() override
    {
        Text::StringBuilder^ sb = gcnew Text::StringBuilder();

        for each (auto elem in list) {
            if (!single) {
                sb->AppendFormat("device: {0}\n", elem->settings->id);
                if (!String::IsNullOrWhiteSpace(elem->settings->name)) {
                    sb->AppendFormat("  name: {0}\n", elem->settings->name);
                }
            }

            for each (auto msg in elem->messages) {
                sb->AppendFormat("\tx: {0}\n", msg);
            }
        }

        return sb->ToString();
    }
};

struct accel_instance_t {
    ra::modifier mod;
    ra::driver_settings settings;

    accel_instance_t() = default;

    accel_instance_t(ra::driver_settings& args) :
        settings(args),
        mod(args) {}

    void init(Profile^ args)
    {
        Marshal::StructureToPtr(args, IntPtr(&settings.prof), false);
        ra::init_data(settings);
        mod = { settings };
    }
};

public ref class ManagedAccel
{
    accel_instance_t* const instance = new accel_instance_t();
public:

    ManagedAccel() {}

    ManagedAccel(ra::driver_settings& settings) : 
        instance(new accel_instance_t(settings)) {}

    ManagedAccel(Profile^ settings)
    {
        Settings = settings;
    }

    virtual ~ManagedAccel()
    {
        delete instance;
    }

    !ManagedAccel()
    {
        delete instance;
    }

    Tuple<double, double>^ Accelerate(int x, int y, double dpi_factor, double time)
    {
        vec2d in_out_vec = {
            (double)x,
            (double)y
        };

        instance->mod.modify(in_out_vec, instance->settings, dpi_factor, time);

        return gcnew Tuple<double, double>(in_out_vec.x, in_out_vec.y);
    }

    property Profile^ Settings
    {
        Profile^ get()
        {
            return gcnew Profile(instance->settings.prof);
        }

        void set(Profile^ val)
        {
            instance->init(val);
        }

    }

    ra::driver_settings GetSettings()
    {
        return instance->settings;
    }

};


[JsonObject(ItemRequired = Required::Always)]
public ref class DriverConfig {
public:
    literal double WriteDelayMs = ra::WRITE_DELAY;
    literal int MaxProfiles = ra::DRIVER_CAPACITY;
    literal int MaxDevices = ra::DEVICE_CAPACITY;
    literal String^ Key = "Driver settings";

    String^ version = RA_VER_STRING;

    DeviceConfig defaultDeviceConfig;

    List<Profile^>^ profiles;

    [NonSerialized]
    List<ManagedAccel^>^ accels;

    List<DeviceSettings^>^ devices;

    void Activate()
    {
        ra::io_t* data = static_cast<ra::io_t*>(malloc(sizeof(ra::io_t)));

        if (!data) throw gcnew Exception("bad alloc");
        
        data->default_dev_cfg.disable = defaultDeviceConfig.disable;
        data->default_dev_cfg.set_extra_info = defaultDeviceConfig.setExtraInfo;
        data->default_dev_cfg.dpi = defaultDeviceConfig.dpi;
        data->default_dev_cfg.polling_rate = defaultDeviceConfig.pollingRate;
        data->default_dev_cfg.clamp.min = defaultDeviceConfig.minimumTime;
        data->default_dev_cfg.clamp.max = defaultDeviceConfig.maximumTime;

        data->driver_data_size = profiles->Count;
        data->device_data_size = devices->Count;

        for (auto i = 0; i < profiles->Count; i++) {
            auto& drv_settings = data->driver_data[i];
            drv_settings = accels[i]->GetSettings();
        }

        for (auto i = 0; i < devices->Count; i++) {
            auto& dev_settings = data->device_data[i];
            Marshal::StructureToPtr(devices[i], IntPtr(&dev_settings), false);
        }

        try {
            ra::write(*data);
            free(data);
        }
        catch (const ra::error& e) {
            free(data);
            throw gcnew InteropException(e);
        }
    }

    // returns null or a joined list of error messages
    String^ Errors()
    {
        Text::StringBuilder^ sb = gcnew Text::StringBuilder();

        if (profiles->Count > MaxProfiles) {
            sb->AppendFormat("Number of profiles ({0}) exceeds max ({1})\n", profiles->Count, MaxProfiles);
        }

        if (devices->Count > MaxDevices) {
            sb->AppendFormat("Number of devices ({0}) exceeds max ({1})\n", devices->Count, MaxDevices);
        }

        ProfileErrors^ profErrors = gcnew ProfileErrors(profiles);
        if (!profErrors->Empty()) {
            sb->Append(profErrors->ToString());
        }

        DeviceSettings^ defaultDev = gcnew DeviceSettings();
        defaultDev->config = defaultDeviceConfig;
        defaultDev->id = "Default";
        devices->Add(defaultDev);

        DeviceErrors^ devErrors = gcnew DeviceErrors(devices);
        if (!devErrors->Empty()) {
            sb->Append(profErrors->ToString());
        }

        devices->RemoveAt(devices->Count - 1);

        if (sb->Length == 0) {
            return nullptr;
        }
        else {
            return sb->ToString();
        }
    }

    JObject^ ToJObject()
    {
        auto dataQueue = gcnew Queue<array<float>^>();
        auto empty = Array::Empty<float>();

        for each (auto prof in profiles) {
            if (prof->argsX.mode == AccelMode::lut) {
                // data->Length is fixed for interop, 
                // temporary resize avoids serializing a bunch of zeros
                Array::Resize(prof->argsX.data, prof->argsX.length);
            }
            else {
                // table data may be used internally in any mode, 
                // so hide it when it's not needed for deserialization 
                dataQueue->Enqueue(prof->argsX.data);
                prof->argsX.data = empty;
            }

            if (prof->argsY.mode == AccelMode::lut) {
                Array::Resize(prof->argsY.data, prof->argsY.length);
            }
            else {
                dataQueue->Enqueue(prof->argsY.data);
                prof->argsY.data = empty;
            }
        }

        JObject^ jObject = JObject::FromObject(this);
        String^ capModes = String::Join(" | ", Enum::GetNames(ClassicCapMode::typeid));
        String^ accelModes = String::Join(" | ", Enum::GetNames(AccelMode::typeid));
        jObject->AddFirst(gcnew JProperty("### Cap modes (applies to classic only) ###", capModes));
        jObject->AddFirst(gcnew JProperty("### Accel modes ###", accelModes));

        for each (auto prof in profiles) {
            if (prof->argsX.mode == AccelMode::lut) {
                Array::Resize(prof->argsX.data, ra::LUT_RAW_DATA_CAPACITY);
            }
            else {
                prof->argsX.data = dataQueue->Dequeue();
            }

            if (prof->argsY.mode == AccelMode::lut) {
                Array::Resize(prof->argsY.data, ra::LUT_RAW_DATA_CAPACITY);
            }
            else {
                prof->argsY.data = dataQueue->Dequeue();
            }
        }

        return jObject;
    }

    String^ ToJSON()
    {
        return ToJObject()->ToString();
    }

    // returns (config, null) or (null, error message)
    static Tuple<DriverConfig^, String^>^ Convert(String^ json)
    {
        auto jss = gcnew JsonSerializerSettings();
        jss->DefaultValueHandling = DefaultValueHandling::Populate;
        auto cfg = JsonConvert::DeserializeObject<DriverConfig^>(json, jss);
        if (cfg == nullptr) throw gcnew JsonException("invalid JSON");

        auto message = cfg->Errors();
        if (message != nullptr) {
            return gcnew Tuple<DriverConfig^, String^>(nullptr, message);
        }
        else {
            cfg->accels = gcnew List<ManagedAccel^>();

            if (cfg->profiles->Count == 0) {
                cfg->profiles->Add(gcnew Profile());
            }

            for each (auto prof in cfg->profiles) {
                cfg->accels->Add(gcnew ManagedAccel(prof));
            }
            return gcnew Tuple<DriverConfig^, String^>(cfg, nullptr);
        }
    }

    static DriverConfig^ GetActive()
    {
        ra::io_t* data = static_cast<ra::io_t*>(malloc(sizeof(ra::io_t)));

        if (!data) throw gcnew Exception("io_t alloc failed");

        try {
            ra::read(*data);
        }
        catch (const ra::error& e) {
            free(data);
            throw gcnew InteropException(e);
        } 

        auto cfg = gcnew DriverConfig();
        cfg->profiles = gcnew List<Profile^>();
        cfg->accels = gcnew List<ManagedAccel^>();
        cfg->devices = gcnew List<DeviceSettings^>();

        for (auto i = 0u; i < data->driver_data_size; i++) {
            auto& drv_settings = data->driver_data[i];
            cfg->profiles->Add(gcnew Profile(drv_settings.prof));
            cfg->accels->Add(gcnew ManagedAccel(drv_settings));
        }

        for (auto i = 0u; i < data->device_data_size; i++) {
            auto& dev_settings = data->device_data[i];
            cfg->devices->Add(gcnew DeviceSettings(dev_settings));
        }

        cfg->defaultDeviceConfig.Init(data->default_dev_cfg);

        free(data);
        return cfg;
    }

    static DriverConfig^ GetDefault()
    {
        auto cfg = gcnew DriverConfig();
        cfg->profiles = gcnew List<Profile^>();
        cfg->accels = gcnew List<ManagedAccel^>();
        cfg->devices = gcnew List<DeviceSettings^>();

        cfg->profiles->Add(gcnew Profile());
        cfg->accels->Add(gcnew ManagedAccel(default_driver_settings));
        cfg->defaultDeviceConfig.Init(default_device_settings.config);
        return cfg;
    }

private: 
    DriverConfig() {}
};

