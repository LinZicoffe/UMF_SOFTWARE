namespace UMF流量计.Helpers;

public static class ModbusRegisterConverter
{
    /// <summary>
    /// MCU FC06 存储: str[0]=Rx[5]=val_lo, str[1]=Rx[4]=val_hi
    /// 所以寄存器值 reg = (val_hi<<8)|val_lo，即 bytes[0]=reg_lo, bytes[1]=reg_hi
    /// 反过来: byte_lo = reg & 0xFF, byte_hi = reg >> 8
    /// </summary>
    public static float RegistersToFloat(ushort reg0, ushort reg1)
    {
        byte[] bytes = new byte[4];
        bytes[0] = (byte)(reg0 & 0xFF);   // str[0] = val_lo of reg0
        bytes[1] = (byte)(reg0 >> 8);       // str[1] = val_hi of reg0
        bytes[2] = (byte)(reg1 & 0xFF);    // str[2] = val_lo of reg1
        bytes[3] = (byte)(reg1 >> 8);       // str[3] = val_hi of reg1
        return BitConverter.ToSingle(bytes, 0);
    }

    /// <summary>
    /// MCU 期望: str[0]=lo, str[1]=hi → reg = (hi<<8)|lo = (bytes[1]<<8)|bytes[0]
    /// float 20.0 → bytes=[0x00,0x00,0xA0,0x41] (little-endian)
    /// reg0 = (bytes[1]<<8)|bytes[0] = 0x0000, reg1 = (bytes[3]<<8)|bytes[2] = 0x41A0
    /// </summary>
    public static (ushort reg0, ushort reg1) FloatToRegisters(float value)
    {
        byte[] bytes = BitConverter.GetBytes(value);
        ushort reg0 = (ushort)((bytes[1] << 8) | bytes[0]);
        ushort reg1 = (ushort)((bytes[3] << 8) | bytes[2]);
        return (reg0, reg1);
    }

    public static ulong RegistersToUInt64Special(ushort[] registers)
    {
        if (registers.Length < 4)
            throw new ArgumentException("需要至少4个寄存器来解析累积流量", nameof(registers));

        byte[] txBuffer = new byte[8];
        txBuffer[0] = (byte)(registers[0] >> 8);
        txBuffer[1] = (byte)(registers[0] & 0xFF);
        txBuffer[2] = (byte)(registers[1] >> 8);
        txBuffer[3] = (byte)(registers[1] & 0xFF);
        txBuffer[4] = (byte)(registers[2] >> 8);
        txBuffer[5] = (byte)(registers[2] & 0xFF);
        txBuffer[6] = (byte)(registers[3] >> 8);
        txBuffer[7] = (byte)(registers[3] & 0xFF);

        ulong result = 0;
        result |= ((ulong)txBuffer[0]) << 40;
        result |= ((ulong)txBuffer[1]) << 32;
        result |= ((ulong)txBuffer[2]) << 56;
        result |= ((ulong)txBuffer[3]) << 48;
        result |= ((ulong)txBuffer[4]) << 8;
        result |= ((ulong)txBuffer[5]) << 0;
        result |= ((ulong)txBuffer[6]) << 24;
        result |= ((ulong)txBuffer[7]) << 16;

        return result;
    }
}
