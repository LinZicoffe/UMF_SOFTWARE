using System.Collections.Concurrent;
using Serilog;
using UMF流量计.Communication;
using UMF流量计.Models;

namespace UMF流量计.Services;

public class DataService : IDataService
{
    private readonly IModbusService _modbusService;
    private CancellationTokenSource? _cts;
    private int _consecutiveFailures;
    private const int MaxConsecutiveFailures = 3;
    private int _cumulativeReadCounter;

    public SensorData? LatestData { get; private set; }
    public ulong CumulativeFlow { get; private set; }
    public bool IsRunning { get; private set; }
    public bool IsConnected => _modbusService.IsConnected;

    public event EventHandler<SensorData>? DataUpdated;
    public event EventHandler<ulong>? CumulativeFlowUpdated;
    public event EventHandler<Exception>? ConnectionLost;
    public event EventHandler<bool>? ConnectionStateChanged;

    public DataService(IModbusService modbusService)
    {
        _modbusService = modbusService;
        _modbusService.ConnectionStateChanged += (s, connected) => ConnectionStateChanged?.Invoke(this, connected);
    }

    public async Task ConnectAsync(ConnectionConfig config)
    {
        await _modbusService.ConnectAsync(config);
    }

    public async Task DisconnectAsync()
    {
        await StopAsync();
        await _modbusService.DisconnectAsync();
    }

    public async Task StartAsync(int intervalMs = 1000, CancellationToken ct = default)
    {
        if (IsRunning) return;

        _cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        IsRunning = true;
        _consecutiveFailures = 0;
        _cumulativeReadCounter = 0;

        Log.Information("数据采集已启动，间隔: {Interval}ms", intervalMs);

        try
        {
            while (!_cts.Token.IsCancellationRequested)
            {
                try
                {
                    await PollAsync(_cts.Token);
                    _consecutiveFailures = 0;
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    _consecutiveFailures++;
                    Log.Warning(ex, "轮询失败 ({Count}/{Max})", _consecutiveFailures, MaxConsecutiveFailures);

                    if (_consecutiveFailures >= MaxConsecutiveFailures)
                    {
                        Log.Error("连续 {Count} 次轮询失败，判定连接丢失", _consecutiveFailures);
                        ConnectionLost?.Invoke(this, ex);
                        _consecutiveFailures = 0;
                    }
                }

                await Task.Delay(intervalMs, _cts.Token);
            }
        }
        catch (OperationCanceledException) { }
        finally
        {
            IsRunning = false;
            Log.Information("数据采集已停止");
        }
    }

    public Task StopAsync()
    {
        _cts?.Cancel();
        IsRunning = false;
        return Task.CompletedTask;
    }

    private async Task PollAsync(CancellationToken ct)
    {
        // 先读取模拟寄存器判断是否开启了模拟模式
        var simData = await _modbusService.ReadSimulationDataAsync(ct);

        SensorData sensorData;
        if (simData.IsEnabled)
        {
            // 模拟模式开启 → 用模拟寄存器的值填充仪表盘
            sensorData = new SensorData
            {
                FlowRate = simData.SimFlowRate,
                Temperature = simData.SimTemperature,
                Pressure = 0f,
                Timestamp = DateTime.Now
            };
        }
        else
        {
            // 正常模式 → 读真实传感器寄存器 0-5
            sensorData = await _modbusService.ReadSensorDataAsync(ct);
        }

        LatestData = sensorData;
        DataUpdated?.Invoke(this, sensorData);

        _cumulativeReadCounter++;
        if (_cumulativeReadCounter % 5 == 0)
        {
            ulong cumulative;
            if (simData.IsEnabled)
            {
                // 模拟模式：用模拟累积流量
                cumulative = (ulong)simData.SimCumulativeFlow;
            }
            else
            {
                cumulative = await _modbusService.ReadCumulativeFlowAsync(ct);
            }
            CumulativeFlow = cumulative;
            CumulativeFlowUpdated?.Invoke(this, cumulative);
        }
    }

    public ValueTask DisposeAsync()
    {
        _cts?.Cancel();
        _cts?.Dispose();
        return _modbusService.DisposeAsync();
    }
}
