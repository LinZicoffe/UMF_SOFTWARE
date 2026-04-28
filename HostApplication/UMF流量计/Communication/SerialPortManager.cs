using System.IO.Ports;

namespace UMF流量计.Communication;

public class SerialPortManager
{
    private SerialPort? _serialPort;

    public string[] GetAvailablePorts()
    {
        return SerialPort.GetPortNames();
    }

    public SerialPort OpenPort(string portName, int baudRate, int dataBits, Parity parity, StopBits stopBits)
    {
        ClosePort();

        _serialPort = new SerialPort(portName, baudRate, parity, dataBits, stopBits)
        {
            ReadTimeout = 1000,
            WriteTimeout = 1000,
            Encoding = System.Text.Encoding.Default
        };
        _serialPort.Open();
        return _serialPort;
    }

    public void ClosePort()
    {
        if (_serialPort != null && _serialPort.IsOpen)
        {
            try
            {
                _serialPort.Close();
                _serialPort.Dispose();
            }
            catch
            {
            }
        }
        _serialPort = null;
    }

    public bool IsOpen => _serialPort?.IsOpen ?? false;

    public SerialPort? Port => _serialPort;
}
