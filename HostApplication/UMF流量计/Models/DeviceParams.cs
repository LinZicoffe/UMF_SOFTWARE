namespace UMF流量计.Models;

/// <summary>
/// 设备参数 (寄存器 22~29)
/// </summary>
public class DeviceParams
{
    /// <summary>流量单位 (0=m³/h, 1=L/h, 2=L/min, 3=kg/h)</summary>
    public ushort FlowUnit { get; set; }

    /// <summary>累积单位 (0=m³, 1=L, 2=kg, 3=t)</summary>
    public ushort CumulativeUnit { get; set; }

    /// <summary>仪表系数 (范围 0.001~99.999)</summary>
    public float MeterCoefficient { get; set; }

    /// <summary>介质系数 (范围 0.100~10.000)</summary>
    public float MediumCoefficient { get; set; }

    /// <summary>小信号切除 (范围 0.0~10.0 %)</summary>
    public float SmallSignalCutoff { get; set; }

    public static readonly string[] FlowUnitNames = { "m³/h", "L/h", "L/min", "kg/h" };
    public static readonly string[] CumulativeUnitNames = { "m³", "L", "kg", "t" };
}
