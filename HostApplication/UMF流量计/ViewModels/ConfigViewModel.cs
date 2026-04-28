using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Serilog;
using UMF流量计.Communication;
using UMF流量计.Models;

namespace UMF流量计.ViewModels;

public partial class ConfigViewModel : ObservableObject
{
    private readonly IModbusService _modbusService;

    // 量程配置
    [ObservableProperty]
    private float _spanLo;

    [ObservableProperty]
    private float _spanHi;

    [ObservableProperty]
    private bool _isCalibrationMode;

    // 设备参数
    [ObservableProperty]
    private int _flowUnit;

    [ObservableProperty]
    private int _cumulativeUnit;

    [ObservableProperty]
    private float _meterCoefficient;

    [ObservableProperty]
    private float _mediumCoefficient;

    [ObservableProperty]
    private float _smallSignalCutoff;

    public string[] FlowUnitOptions => DeviceParams.FlowUnitNames;
    public string[] CumulativeUnitOptions => DeviceParams.CumulativeUnitNames;

    public ConfigViewModel(IModbusService modbusService)
    {
        _modbusService = modbusService;
    }

    [RelayCommand]
    private async Task ReadSpanConfig()
    {
        try
        {
            var config = await _modbusService.ReadSpanConfigAsync();
            SpanLo = config.SpanLo;
            SpanHi = config.SpanHi;

            var coils = await _modbusService.ReadCoilsAsync();
            IsCalibrationMode = coils.CalEnabled;

            HandyControl.Controls.Growl.Success("已读取量程配置");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取量程配置失败");
        }
    }

    [RelayCommand]
    private async Task WriteSpanConfig()
    {
        try
        {
            var coils = await _modbusService.ReadCoilsAsync();
            if (!coils.CalEnabled)
            {
                HandyControl.Controls.Growl.Warning("请先在校准页面进入校准模式");
                return;
            }
            IsCalibrationMode = true;

            await _modbusService.WriteSpanConfigAsync(SpanLo, SpanHi);
            HandyControl.Controls.Growl.Success($"量程已写入: {SpanLo} ~ {SpanHi}");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入量程配置失败");
        }
    }

    [RelayCommand]
    private async Task ReadDeviceParams()
    {
        try
        {
            var param = await _modbusService.ReadDeviceParamsAsync();
            FlowUnit = param.FlowUnit;
            CumulativeUnit = param.CumulativeUnit;
            MeterCoefficient = param.MeterCoefficient;
            MediumCoefficient = param.MediumCoefficient;
            SmallSignalCutoff = param.SmallSignalCutoff;

            HandyControl.Controls.Growl.Success("已读取设备参数");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取设备参数失败");
        }
    }

    [RelayCommand]
    private async Task WriteDeviceParams()
    {
        try
        {
            if (MeterCoefficient < 0.001f || MeterCoefficient > 99.999f)
            {
                HandyControl.Controls.Growl.Warning("仪表系数范围: 0.001~99.999");
                return;
            }
            if (MediumCoefficient < 0.1f || MediumCoefficient > 10.0f)
            {
                HandyControl.Controls.Growl.Warning("介质系数范围: 0.100~10.000");
                return;
            }
            if (SmallSignalCutoff < 0f || SmallSignalCutoff > 10f)
            {
                HandyControl.Controls.Growl.Warning("小信号切除范围: 0.0~10.0 %");
                return;
            }

            var param = new DeviceParams
            {
                FlowUnit = (ushort)FlowUnit,
                CumulativeUnit = (ushort)CumulativeUnit,
                MeterCoefficient = MeterCoefficient,
                MediumCoefficient = MediumCoefficient,
                SmallSignalCutoff = SmallSignalCutoff
            };
            await _modbusService.WriteDeviceParamsAsync(param);
            HandyControl.Controls.Growl.Success("设备参数已写入");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入设备参数失败");
        }
    }
}
