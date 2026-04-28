using System.IO.Ports;
using FluentModbus;
using Serilog;
using UMF流量计.Helpers;
using UMF流量计.Models;

namespace UMF流量计.Communication;

public class ModbusRtuService : IModbusService
{
    private ModbusRtuClient? _client;
    private ConnectionConfig? _config;
    private bool _isConnected;

    public bool IsConnected => _isConnected;
    public event EventHandler<bool>? ConnectionStateChanged;

    public ModbusRtuService()
    {
    }

    public async Task ConnectAsync(ConnectionConfig config, CancellationToken ct = default)
    {
        await Task.Run(() =>
        {
            try
            {
                _client = new ModbusRtuClient
                {
                    BaudRate = config.BaudRate,
                    Parity = config.Parity,
                    StopBits = config.StopBits
                };
                _client.Connect(config.PortName, ModbusEndianness.BigEndian);
                _config = config;

                SetConnected(true);
                Log.Information("已连接到 {Port} @ {BaudRate}, 从站地址: {Slave}", config.PortName, config.BaudRate, config.SlaveAddress);
            }
            catch (Exception ex)
            {
                SetConnected(false);
                Log.Warning(ex, "连接串口失败: {Port}", config.PortName);
            }
        }, ct);
    }

    public Task DisconnectAsync()
    {
        try
        {
            _client?.Close();
            _client?.Dispose();
        }
        catch { }
        _client = null;
        SetConnected(false);
        Log.Information("已断开串口连接");
        return Task.CompletedTask;
    }

    public async Task<SensorData> ReadSensorDataAsync(CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return new SensorData { Timestamp = DateTime.Now };

            var data = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 0, 6).ToArray(), ct);
            return new SensorData
            {
                FlowRate = ModbusRegisterConverter.RegistersToFloat((ushort)data[0], (ushort)data[1]),
                Temperature = ModbusRegisterConverter.RegistersToFloat((ushort)data[2], (ushort)data[3]),
                Pressure = ModbusRegisterConverter.RegistersToFloat((ushort)data[4], (ushort)data[5]),
                Timestamp = DateTime.Now
            };
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取传感器数据异常");
            return new SensorData { Timestamp = DateTime.Now };
        }
    }

    public async Task<DacCalibration> ReadDacCalibrationAsync(CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return new DacCalibration();

            var data = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 20, 2).ToArray(), ct);
            return new DacCalibration
            {
                ZeroValue = (ushort)data[0],
                FullValue = (ushort)data[1]
            };
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取DAC校准数据异常");
            return new DacCalibration();
        }
    }

    public async Task<SpanConfig> ReadSpanConfigAsync(CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return new SpanConfig();

            var data = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 30, 4).ToArray(), ct);
            return new SpanConfig
            {
                SpanLo = ModbusRegisterConverter.RegistersToFloat((ushort)data[0], (ushort)data[1]),
                SpanHi = ModbusRegisterConverter.RegistersToFloat((ushort)data[2], (ushort)data[3])
            };
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取量程配置异常");
            return new SpanConfig();
        }
    }

    public async Task<ulong> ReadCumulativeFlowAsync(CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return 0;

            var data = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 40, 4).ToArray(), ct);
            ushort[] regs = data.Select(s => (ushort)s).ToArray();
            return ModbusRegisterConverter.RegistersToUInt64Special(regs);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取累积流量异常");
            return 0;
        }
    }

    public async Task<CoilState> ReadCoilsAsync(CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return new CoilState();

            var data = await Task.Run(() =>
                _client!.ReadCoils(_config!.SlaveAddress, 0, 6).ToArray(), ct);
            return new CoilState
            {
                CalEnabled = data[0] != 0,
                ForceOutput4mA = data[1] != 0,
                ForceOutput20mA = data[2] != 0,
                FlowClearCmd = data[3] != 0,
                FlowResetCmd = data[4] != 0,
                PassiveReadMode = data[5] != 0
            };
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取线圈状态异常");
            return new CoilState();
        }
    }

    public async Task<SimulationData> ReadSimulationDataAsync(CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return new SimulationData();

            var switchData = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 48, 1).ToArray(), ct);
            bool isEnabled = switchData[0] != 0;

            var flowData = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 50, 2).ToArray(), ct);
            float simFlow = ModbusRegisterConverter.RegistersToFloat((ushort)flowData[0], (ushort)flowData[1]);

            var tempData = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 52, 2).ToArray(), ct);
            float simTemp = ModbusRegisterConverter.RegistersToFloat((ushort)tempData[0], (ushort)tempData[1]);

            var cumulData = await Task.Run(() =>
                _client!.ReadHoldingRegisters<short>(_config!.SlaveAddress, 54, 2).ToArray(), ct);
            float simCumul = ModbusRegisterConverter.RegistersToFloat((ushort)cumulData[0], (ushort)cumulData[1]);

            return new SimulationData
            {
                IsEnabled = isEnabled,
                SimFlowRate = simFlow,
                SimTemperature = simTemp,
                SimCumulativeFlow = simCumul
            };
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "读取模拟数据异常");
            return new SimulationData();
        }
    }

    public async Task WriteCoilAsync(ushort address, bool value, CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return;
            await Task.Run(() =>
                _client!.WriteSingleCoil(_config!.SlaveAddress, address, value), ct);
            Log.Information("写入线圈 {Address} = {Value}", address, value);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入线圈 {Address} 异常", address);
        }
    }

    public async Task WriteDacCalibrationAsync(ushort zeroValue, ushort fullValue, CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return;
            await Task.Run(() =>
                _client!.WriteMultipleRegisters(_config!.SlaveAddress, 20, new short[] { (short)zeroValue, (short)fullValue }), ct);
            Log.Information("写入DAC校准: 零点={Zero}, 满度={Full}", zeroValue, fullValue);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入DAC校准异常");
        }
    }

    public async Task WriteSpanConfigAsync(float spanLo, float spanHi, CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return;
            var (loHi, loLo) = ModbusRegisterConverter.FloatToRegisters(spanLo);
            var (hiHi, hiLo) = ModbusRegisterConverter.FloatToRegisters(spanHi);
            await Task.Run(() =>
                _client!.WriteMultipleRegisters(_config!.SlaveAddress, 30,
                    new short[] { (short)loHi, (short)loLo, (short)hiHi, (short)hiLo }), ct);
            Log.Information("写入量程配置: 下限={Lo}, 上限={Hi}", spanLo, spanHi);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入量程配置异常");
        }
    }

    public async Task WriteSimulationSwitchAsync(bool enabled, CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return;
            await Task.Run(() =>
                _client!.WriteSingleRegister(_config!.SlaveAddress, 48,
                    (short)(enabled ? 1 : 0)), ct);
            Log.Information("写入模拟总开关(FC06): {Enabled}", enabled);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入模拟总开关异常");
        }
    }

    public async Task WriteSimulationFlowAsync(float flowRate, CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return;
            var (reg0, reg1) = ModbusRegisterConverter.FloatToRegisters(flowRate);
            await Task.Run(() =>
            {
                _client!.WriteSingleRegister(_config!.SlaveAddress, 50, (short)reg0);
                _client!.WriteSingleRegister(_config!.SlaveAddress, 51, (short)reg1);
            }, ct);
            Log.Information("写入模拟流量(FC06): {Flow}", flowRate);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入模拟流量异常");
        }
    }

    public async Task WriteSimulationTemperatureAsync(float temperature, CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return;
            var (reg0, reg1) = ModbusRegisterConverter.FloatToRegisters(temperature);
            await Task.Run(() =>
            {
                _client!.WriteSingleRegister(_config!.SlaveAddress, 52, (short)reg0);
                _client!.WriteSingleRegister(_config!.SlaveAddress, 53, (short)reg1);
            }, ct);
            Log.Information("写入模拟温度(FC06): {Temp}", temperature);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入模拟温度异常");
        }
    }

    public async Task WriteSimulationCumulativeAsync(float cumulative, CancellationToken ct = default)
    {
        try
        {
            if (!IsReady()) return;
            var (reg0, reg1) = ModbusRegisterConverter.FloatToRegisters(cumulative);
            await Task.Run(() =>
            {
                _client!.WriteSingleRegister(_config!.SlaveAddress, 54, (short)reg0);
                _client!.WriteSingleRegister(_config!.SlaveAddress, 55, (short)reg1);
            }, ct);
            Log.Information("写入模拟累积流量(FC06): {Cumulative}", cumulative);
        }
        catch (Exception ex)
        {
            Log.Debug(ex, "写入模拟累积流量异常");
        }
    }

    public ValueTask DisposeAsync()
    {
        try
        {
            _client?.Close();
            _client?.Dispose();
        }
        catch { }
        SetConnected(false);
        return ValueTask.CompletedTask;
    }

    private bool IsReady()
    {
        return _isConnected && _client != null;
    }

    private void SetConnected(bool connected)
    {
        if (_isConnected != connected)
        {
            _isConnected = connected;
            ConnectionStateChanged?.Invoke(this, connected);
        }
    }
}
