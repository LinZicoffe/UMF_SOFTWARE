using UMF流量计.Models;

namespace UMF流量计.Services;

public interface IDataService : IAsyncDisposable
{
    SensorData? LatestData { get; }
    ulong CumulativeFlow { get; }
    bool IsRunning { get; }
    bool IsConnected { get; }

    event EventHandler<SensorData>? DataUpdated;
    event EventHandler<ulong>? CumulativeFlowUpdated;
    event EventHandler<Exception>? ConnectionLost;
    event EventHandler<bool>? ConnectionStateChanged;

    Task StartAsync(int intervalMs = 1000, CancellationToken ct = default);
    Task StopAsync();
    Task ConnectAsync(ConnectionConfig config);
    Task DisconnectAsync();
}
