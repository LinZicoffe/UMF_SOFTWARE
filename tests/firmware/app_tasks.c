/* 编译完整生产 main.c；包装 static 任务供离线测试调用，不复制业务逻辑。 */
/* 忙等待由 doubles.c 记录请求，生产延时函数在本测试中不调用。 */
#define Time_Delay sim_unused_busy_delay
#include "../../Core/Src/main.c"
#undef Time_Delay

void sim_app_display_task(void)
{
    app_display_task();
}

void sim_app_output_task(void)
{
    app_output_task();
}
