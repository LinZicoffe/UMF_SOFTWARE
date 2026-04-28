using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Serilog;
using UMF流量计.Communication;

namespace UMF流量计.ViewModels;

public partial class SimulationViewModel : ObservableObject
{
    private readonly IModbusService _modbusService;

    [ObservableProperty]
    private bool _simulationEnabled;

    [ObservableProperty]
    private float _simFlowRate;

    [ObservableProperty]
    private float _simTemperature;

    [ObservableProperty]
    private float _simCumulativeFlow;

    public SimulationViewModel(IModbusService modbusService)
    {
        _modbusService = modbusService;
    }

    [RelayCommand]
    private async Task ToggleSimulation()
    {
        try
        {
            await _modbusService.WriteSimulationSwitchAsync(SimulationEnabled);
            HandyControl.Controls.Growl.Success($"模拟模式已{(SimulationEnabled ? "开启" : "关闭")}");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "切换模拟模式失败");
        }
    }

    [RelayCommand]
    private async Task WriteSimFlowRate()
    {
        try
        {
            await _modbusService.WriteSimulationFlowAsync(SimFlowRate);
            HandyControl.Controls.Growl.Success($"模拟流量已写入: {SimFlowRate}");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入模拟流量失败");
        }
    }

    [RelayCommand]
    private async Task WriteSimTemperature()
    {
        try
        {
            await _modbusService.WriteSimulationTemperatureAsync(SimTemperature);
            HandyControl.Controls.Growl.Success($"模拟温度已写入: {SimTemperature}");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入模拟温度失败");
        }
    }

    [RelayCommand]
    private async Task WriteSimCumulative()
    {
        try
        {
            await _modbusService.WriteSimulationCumulativeAsync(SimCumulativeFlow);
            HandyControl.Controls.Growl.Success($"模拟累积流量已写入: {SimCumulativeFlow}");
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入模拟累积流量失败");
        }
    }
}
