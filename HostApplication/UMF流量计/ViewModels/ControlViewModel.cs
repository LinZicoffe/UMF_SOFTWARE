using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Serilog;
using UMF流量计.Communication;
using UMF流量计.Models;

namespace UMF流量计.ViewModels;

public partial class ControlViewModel : ObservableObject
{
    private readonly IModbusService _modbusService;

    [ObservableProperty]
    private CoilState? _coilState;

    public ControlViewModel(IModbusService modbusService)
    {
        _modbusService = modbusService;
    }

    [RelayCommand]
    private async Task RefreshCoils()
    {
        try
        {
            CoilState = await _modbusService.ReadCoilsAsync();
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取线圈状态失败");
        }
    }

    [RelayCommand]
    private async Task ClearCumulative()
    {
        try
        {
            await _modbusService.WriteCoilAsync(3, true);
            HandyControl.Controls.Growl.Success("已发送累积清零命令");
            await RefreshCoils();
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "累积清零失败");
        }
    }

    [RelayCommand]
    private async Task ResetModule()
    {
        try
        {
            await _modbusService.WriteCoilAsync(4, true);
            HandyControl.Controls.Growl.Success("已发送模组复位命令");
            await RefreshCoils();
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "模组复位失败");
        }
    }

    [RelayCommand]
    private async Task TogglePassiveMode()
    {
        try
        {
            if (CoilState == null)
                await RefreshCoils();

            var newState = !(CoilState?.PassiveReadMode ?? false);
            await _modbusService.WriteCoilAsync(5, newState);
            HandyControl.Controls.Growl.Success($"已切换为{(newState ? "被动" : "主动")}模式");
            await RefreshCoils();
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "切换模式失败");
        }
    }
}
