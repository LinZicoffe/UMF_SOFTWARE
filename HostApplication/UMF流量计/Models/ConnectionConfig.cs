namespace UMF流量计.Models;

public class ConnectionConfig
{
    public string PortName { get; set; } = "COM3";
    public int BaudRate { get; set; } = 9600;
    public int DataBits { get; set; } = 8;
    public System.IO.Ports.Parity Parity { get; set; } = System.IO.Ports.Parity.None;
    public System.IO.Ports.StopBits StopBits { get; set; } = System.IO.Ports.StopBits.One;
    public byte SlaveAddress { get; set; } = 2;
    public int TimeoutMs { get; set; } = 1000;
    public int PollIntervalMs { get; set; } = 1000;
}
