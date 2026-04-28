namespace UMF流量计.Models;

public class CoilState
{
    public bool CalEnabled { get; init; }
    public bool ForceOutput4mA { get; init; }
    public bool ForceOutput20mA { get; init; }
    public bool FlowClearCmd { get; init; }
    public bool FlowResetCmd { get; init; }
    public bool PassiveReadMode { get; init; }
}
