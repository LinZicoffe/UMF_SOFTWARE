namespace UMF流量计.Models;

public class SensorData
{
    public float FlowRate { get; init; }
    public float Temperature { get; init; }
    public float Pressure { get; init; }
    public DateTime Timestamp { get; init; } = DateTime.Now;
}
