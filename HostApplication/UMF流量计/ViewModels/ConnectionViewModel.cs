using System.IO.Ports;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Serilog;
using UMF流量计.Communication;
using UMF流量计.Models;
using UMF流量计.Services;

namespace UMF流量计.ViewModels;

public partial class ConnectionViewModel : ObservableObject
{
    private readonly IModbusService _modbusService;
    private readonly IDataService _dataService;

    [ObservableProperty]
    private string[] _availablePorts = Array.Empty<string>();

    [ObservableProperty]
    private string _selectedPort = "COM3";

    [ObservableProperty]
    private int _selectedBaudRate = 9600;

    [ObservableProperty]
    private byte _slaveAddress = 2;

    [ObservableProperty]
    private int _pollInterval = 1000;

    [ObservableProperty]
    private bool _isConnected;

    public int[] BaudRates { get; } = { 4800, 9600, 19200, 38400, 115200 };

    public ConnectionViewModel(IModbusService modbusService, IDataService dataService)
    {
        _modbusService = modbusService;
        _dataService = dataService;
        RefreshPorts();
    }

    [RelayCommand]
    private void RefreshPorts()
    {
        AvailablePorts = SerialPort.GetPortNames();
        if (AvailablePorts.Length > 0 && !AvailablePorts.Contains(SelectedPort))
            SelectedPort = AvailablePorts[0];
    }

    [RelayCommand]
    private async Task ConnectAsync()
    {
        try
        {
            var config = new ConnectionConfig
            {
                PortName = SelectedPort,
                BaudRate = SelectedBaudRate,
                SlaveAddress = SlaveAddress,
                PollIntervalMs = PollInterval
            };

            await _dataService.ConnectAsync(config);
            await _dataService.StartAsync(PollInterval);
            IsConnected = true;
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "连接失败");
            IsConnected = false;
        }
    }

    [RelayCommand]
    private async Task DisconnectAsync()
    {
        try
        {
            await _dataService.DisconnectAsync();
            IsConnected = false;
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "断开连接失败");
            IsConnected = false;
        }
    }
}
