using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Serilog;
using UMF流量计.Communication;

namespace UMF流量计.ViewModels;

public partial class ConfigViewModel : ObservableObject
{
    private readonly IModbusService _modbusService;

    [ObservableProperty]
    private float _spanLo;

    [ObservableProperty]
    private float _spanHi;

    [ObservableProperty]
    private bool _isCalibrationMode;

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
}
