using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Serilog;
using UMF流量计.Communication;
using UMF流量计.Models;

namespace UMF流量计.ViewModels;

public partial class CalibrationViewModel : ObservableObject
{
    private readonly IModbusService _modbusService;

    [ObservableProperty]
    private bool _calibrationEnabled;

    [ObservableProperty]
    private ushort _zeroValue;

    [ObservableProperty]
    private ushort _fullValue;

    [ObservableProperty]
    private bool _isForceOutput4mA;

    [ObservableProperty]
    private bool _isForceOutput20mA;

    public CalibrationViewModel(IModbusService modbusService)
    {
        _modbusService = modbusService;
    }

    [RelayCommand]
    private async Task ReadCurrentValues()
    {
        try
        {
            var dac = await _modbusService.ReadDacCalibrationAsync();
            ZeroValue = dac.ZeroValue;
            FullValue = dac.FullValue;

            var coils = await _modbusService.ReadCoilsAsync();
            CalibrationEnabled = coils.CalEnabled;
            IsForceOutput4mA = coils.ForceOutput4mA;
            IsForceOutput20mA = coils.ForceOutput20mA;

            HandyControl.Controls.Growl.Success("已读取当前校准值");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取校准值失败");
        }
    }

    [RelayCommand]
    private async Task EnterCalibrationMode()
    {
        try
        {
            await _modbusService.WriteCoilAsync(0, true);
            CalibrationEnabled = true;
            HandyControl.Controls.Growl.Success("已进入校准模式");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "进入校准模式失败");
        }
    }

    [RelayCommand]
    private async Task ExitCalibrationMode()
    {
        try
        {
            await _modbusService.WriteCoilAsync(0, false);
            CalibrationEnabled = false;
            IsForceOutput4mA = false;
            IsForceOutput20mA = false;
            HandyControl.Controls.Growl.Success("已退出校准模式");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "退出校准模式失败");
        }
    }

    [RelayCommand]
    private async Task ForceOutput4mA()
    {
        try
        {
            await _modbusService.WriteCoilAsync(1, true);
            IsForceOutput4mA = true;
            HandyControl.Controls.Growl.Info("已强制输出 4mA");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "强制输出4mA失败");
        }
    }

    [RelayCommand]
    private async Task ForceOutput20mA()
    {
        try
        {
            await _modbusService.WriteCoilAsync(2, true);
            IsForceOutput20mA = true;
            HandyControl.Controls.Growl.Info("已强制输出 20mA");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "强制输出20mA失败");
        }
    }

    [RelayCommand]
    private async Task WriteZeroValue()
    {
        if (!CalibrationEnabled)
        {
            HandyControl.Controls.Growl.Warning("请先进入校准模式");
            return;
        }
        try
        {
            await _modbusService.WriteDacCalibrationAsync(ZeroValue, FullValue);
            HandyControl.Controls.Growl.Success($"零点值已写入: {ZeroValue}");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入零点值失败");
        }
    }

    [RelayCommand]
    private async Task WriteFullValue()
    {
        if (!CalibrationEnabled)
        {
            HandyControl.Controls.Growl.Warning("请先进入校准模式");
            return;
        }
        try
        {
            await _modbusService.WriteDacCalibrationAsync(ZeroValue, FullValue);
            HandyControl.Controls.Growl.Success($"满度值已写入: {FullValue}");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入满度值失败");
        }
    }
}
