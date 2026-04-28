using UMF流量计.Models;

namespace UMF流量计.Communication;

public interface IModbusService : IAsyncDisposable
{
    bool IsConnected { get; }
    event EventHandler<bool>? ConnectionStateChanged;

    Task ConnectAsync(ConnectionConfig config, CancellationToken ct = default);
    Task DisconnectAsync();

    Task<SensorData> ReadSensorDataAsync(CancellationToken ct = default);
    Task<SimulationData> ReadSimulationDataAsync(CancellationToken ct = default);
    Task<DacCalibration> ReadDacCalibrationAsync(CancellationToken ct = default);
    Task<SpanConfig> ReadSpanConfigAsync(CancellationToken ct = default);
    Task<ulong> ReadCumulativeFlowAsync(CancellationToken ct = default);
    Task<CoilState> ReadCoilsAsync(CancellationToken ct = default);
    Task WriteCoilAsync(ushort address, bool value, CancellationToken ct = default);
    Task WriteDacCalibrationAsync(ushort zeroValue, ushort fullValue, CancellationToken ct = default);
    Task WriteSpanConfigAsync(float spanLo, float spanHi, CancellationToken ct = default);
    Task WriteSimulationSwitchAsync(bool enabled, CancellationToken ct = default);
    Task WriteSimulationFlowAsync(float flowRate, CancellationToken ct = default);
    Task WriteSimulationTemperatureAsync(float temperature, CancellationToken ct = default);
    Task WriteSimulationCumulativeAsync(float cumulative, CancellationToken ct = default);
}
