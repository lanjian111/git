#include "rtc_screen.h" /* 取得屏幕校时和本地时间缓存的公开接口。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */
#include "hmi_driver.h" /* 取得触摸屏命令封装和链路处理接口。 */
#include "elog.h" /* 取得日志级别、格式及输出入口的声明。 */

#define LOG_TAG "rtc" /* 指定日志标签 "rtc"，便于在串口输出中定位本模块。 */
/* 屏幕 RTC 提供日期时间。平时用 STM32 的毫秒节拍推进缓存，定期从屏幕校准。
 * 成功后 5 分钟同步一次。单次等回包 500 ms，超时后 5 秒再试，避免一直占用串口。
 */
#define SYNC_INTERVAL_MS 300000U /* 将同步 间隔 毫秒设为 300000U 毫秒，供时间差比较统一使用。 */
#define REPLY_TIMEOUT_MS 500U /* 将应答 超时 毫秒设为 500U 毫秒，供时间差比较统一使用。 */
#define RETRY_INTERVAL_MS 5000U /* 将重试 间隔 毫秒设为 5000U 毫秒，供时间差比较统一使用。 */
#define MS_PER_SECOND 1000U /* 定义 毫秒 PER 秒 的约定值 1000U，使对应判断或封装统一引用。 */
#define SECONDS_PER_DAY 86400U /* 定义 秒数 PER 日 的约定值 86400U，使对应判断或封装统一引用。 */

/* HMI 串口收到的数据先排队，以下状态只在主循环和它调用的分发函数中访问。 */
static RTC_Time_t screen_time;        /* 最近一次有效校时后继续推进的本地缓存 */
static uint8_t time_valid;             /* 收到第一条合法屏幕回包后才置位 */
static uint32_t clock_tick;            /* 缓存已累计到的最近一个整秒对应的毫秒节拍 */
static uint8_t sync_requested = 1U;    /* 开机主动读一次，屏幕保存时间时也会置位 */
static uint8_t waiting_for_reply;      /* 已送出读命令，目前有一条请求在等待回应 */
static uint32_t request_tick;          /* 当前请求发出的时刻，用于判断回包是否超时 */
static uint32_t schedule_tick;         /* 常规校时或重试的计时起点 */
static uint32_t schedule_interval;     /* 从计时起点到下一次请求应等待的毫秒数 */

static uint8_t decode_bcd(uint8_t value) /* 分别取出 BCD 的高低半字节，转换成十进制值。 */
{
    /* 屏幕用 BCD 表示每个字段，例如 0x25 表示十进制 25。
     * 半字节大于 9 就是非法编码，返回 0xFF 供后续日期和范围校验拒绝。
     */
    uint8_t tens = value >> 4; /* 取 BCD 高半字节作为十进制十位。 */
    uint8_t ones = value & 0x0FU; /* 掩去 BCD 的高半字节，保留十进制个位。 */

    if (tens > 9U || ones > 9U) /* 检查两个半字节是否都是十进制数字，识别非法 BCD 编码。 */
        return 0xFFU; /* 将 「0xFFU」 返回给调用者，供上层使用。 */

    return (uint8_t)(tens * 10U + ones); /* 将 「(uint8_t)(tens * 10U + ones)」 返回给调用者，供上层使用。 */
}

static void advance_clock(uint32_t now) /* 按已过完整秒推进缓存时间，保留不足一秒的节拍差。 */
{
    /* 用无符号节拍做差，SysTick 计数从最大值回到 0 时仍能计算经过时间。 */
    uint32_t elapsed_seconds = (now - clock_tick) / MS_PER_SECOND; /* 用无符号节拍差求完整秒数，毫秒余数留给下一次调用。 */
    uint32_t seconds_today; /* 声明本日累计秒数，为后续计算或读取保存中间值。 */

    if (elapsed_seconds == 0U) /* 不足一秒时暂不更改日历值，节拍余数留到下次推进。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */

    /* 只扣除实际累计的整秒。余下的毫秒留到下次，否则每次调用都会丢掉余数。 */
    clock_tick += elapsed_seconds * MS_PER_SECOND; /* 只消耗已折算成整秒的时间，避免多次推进累计丢失不足一秒的余量。 */
    seconds_today = screen_time.hour * 3600U + screen_time.minute * 60U /* 先把当前小时和分钟折算成从零点起算的秒数。 */
                  + screen_time.second + elapsed_seconds; /* 再累加当前秒字段和新经过的整秒，得到可能跨天的秒数。 */

    /* 将当天秒数归约到 0 至 86399。每跨一天都推进日期和星期。
     * 月末使用公共日历函数处理月长度和闰年，12 月后换到次年 1 月。
     */
    while (seconds_today >= SECONDS_PER_DAY) /* 累计秒数每跨过一天就循环进位一次，支持间隔多天后再调用。 */
    {
        seconds_today -= SECONDS_PER_DAY; /* 扣除一天的秒数，再分别推进星期和日历日期。 */
        screen_time.week = (uint8_t)((screen_time.week + 1U) % 7U); /* 跨天时星期加一，并用模 7 回到一周起点。 */
        if (++screen_time.day > RTC_DaysInMonth(screen_time.year, screen_time.month)) /* 日期加一后与本月实际天数比较，正确处理月末和闰年二月。 */
        {
            screen_time.day = 1U; /* 跨月后日期从 1 日重新开始。 */
            if (++screen_time.month > 12U) /* 月份加一后确认是否越过十二月，以决定是否进位到下一年。 */
            {
                screen_time.month = 1U; /* 跨年后月份从一月重新开始。 */
                screen_time.year++; /* 十二月之后年份加一，使缓存日期连续。 */
            }
        }
    }

    screen_time.hour = (uint8_t)(seconds_today / 3600U); /* 用当天累计秒数除以 3600，得到当前小时。 */
    screen_time.minute = (uint8_t)((seconds_today / 60U) % 60U); /* 换算整分钟后对 60 取余，取得小时内的分钟数。 */
    screen_time.second = (uint8_t)(seconds_today % 60U); /* 对 60 取余取得当前分钟内的秒数。 */
}

void RTC_Screen_Task(void) /* 推进本地时间并检查校时调度，等待屏幕回包期间保持周期任务可运行。 */
{
    uint32_t now = delay_millis(); /* 定义当前时间，把当前毫秒节拍保存到 now，作为之后计算时间差的基准。 */

    /* 校时是在后台推进的。即使屏幕没有回应，已校过的本地时间仍继续走。 */
    if (time_valid) /* 时间缓存已同步时才推进本地走秒，避免给未初始化日期进位。 */
        advance_clock(now); /* 按已过完整秒推进缓存时间，保留不足一秒的节拍差。 */

    /* 同一时刻保留一条请求。等待窗口内立即返回，主循环可继续处理机器控制。
     * 超时则清等待状态、安排延后重试。若此时另有手动同步请求，下面可优先尝试。
     */
    if (waiting_for_reply) /* 存在未完成读时请求时先等待或处理超时，避免发送重叠请求。 */
    {
        if ((now - request_tick) < REPLY_TIMEOUT_MS) /* 仍在应答窗口内则保留请求状态，让以后轮询或回包继续处理。 */
            return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */

        waiting_for_reply = 0U; /* 将正在等待屏幕时间应答的标志 更新为 「0U」。 */
        schedule_tick = now; /* 将校时调度的开始计时点 更新为 「now」。 */
        schedule_interval = RETRY_INTERVAL_MS; /* 将下一次校时前的等待时长 更新为 「RETRY_INTERVAL_MS」。 */
    }

    /* 保存时间的通知或到达周期都可以触发读取。只有 ReadRTC 确认发起发送，
     * 才开始计算回包超时。若屏幕串口正在忙，保留请求条件供下一轮重试。
     */
    if (sync_requested || (now - schedule_tick) >= schedule_interval) /* 收到主动校时请求或到达定期间隔时尝试向屏幕请求时间。 */
    {
        if (ReadRTC()) /* 只有屏幕读时帧成功提交后才开启等待和超时计时。 */
        {
            sync_requested = 0U; /* 将主动向屏幕读时的请求标志 更新为 「0U」。 */
            waiting_for_reply = 1U; /* 将正在等待屏幕时间应答的标志 更新为 「1U」。 */
            request_tick = now; /* 将发送读时请求的时间戳 更新为 「now」。 */
        }
    }
}

void RTC_Screen_RequestSync(void) /* 置主动读时请求，由屏幕 RTC 任务安排发送。 */
{
    /* 回调里只记下重读请求，实际发送统一走 RTC_Screen_Task()。 */
    sync_requested = 1U; /* 将主动向屏幕读时的请求标志 更新为 「1U」。 */
}

void RTC_Screen_OnRead(const uint8_t data[7]) /* 处理屏幕返回的七个 BCD 字节，验证后更新时间缓存。 */
{
    RTC_Time_t received; /* 声明收到的值，为后续计算或读取保存中间值。 */
    uint32_t now = delay_millis(); /* 定义当前时间，把当前毫秒节拍保存到 now，作为之后计算时间差的基准。 */

    /* 只在当前请求的等待窗口内使用读时回包，忽略请求之外或已超时的回包。 */
    if (!waiting_for_reply || (now - request_tick) >= REPLY_TIMEOUT_MS) /* 丢弃没有对应请求或已超过等待窗口的回包，避免旧应答覆盖时间。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */

    /* 7 个字段依照屏幕帧顺序解析。年份只有两位，按 2000 至 2099 解释。 */
    received.year = (uint16_t)(2000U + decode_bcd(data[0])); /* 将年份 更新为 「(uint16_t)(2000U + decode_bcd(data[0]))」。 */
    received.month = decode_bcd(data[1]); /* 将屏幕返回的 BCD 字段解码后写入月份。 */
    received.week = decode_bcd(data[2]); /* 将屏幕返回的 BCD 字段解码后写入星期。 */
    received.day = decode_bcd(data[3]); /* 将屏幕返回的 BCD 字段解码后写入日。 */
    received.hour = decode_bcd(data[4]); /* 将屏幕返回的 BCD 字段解码后写入时。 */
    received.minute = decode_bcd(data[5]); /* 将屏幕返回的 BCD 字段解码后写入分。 */
    received.second = decode_bcd(data[6]); /* 将屏幕返回的 BCD 字段解码后写入秒。 */

    /* 星期只检查编号范围，不与年月日重新匹配。公历日期和时分秒由 RTC_IsValid 校验。
     * 非法回包保留旧缓存和本次等待状态，让同一窗口内后续合法回包仍可生效。 */
    if (received.week > 6U || !RTC_IsValid(&received)) /* 排除星期越界或日期时间无效的应答，避免更新本地时间。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */

    /* 用回包处理时刻重置本地秒节拍基准，并从此时起安排下一次常规同步。 */
    screen_time = received; /* 在回包和日期校验通过后，以屏幕返回时间更新本地时间缓存。 */
    time_valid = 1U; /* 标记本地缓存已取得有效时间，允许上层读取。 */
    clock_tick = now; /* 用接收通过校验的时刻重设本地走秒基准。 */
    waiting_for_reply = 0U; /* 将正在等待屏幕时间应答的标志 更新为 「0U」。 */
    schedule_tick = now; /* 将校时调度的开始计时点 更新为 「now」。 */
    schedule_interval = SYNC_INTERVAL_MS; /* 将下一次校时前的等待时长 更新为 「SYNC_INTERVAL_MS」。 */
    log_i("Screen RTC: %u-%02u-%02u %02u:%02u:%02u", /* 开始构造当前分支的日志记录，格式串后的参数在下一行给出。 */
          (unsigned)received.year, (unsigned)received.month, (unsigned)received.day, /* 用 「(unsigned)received.year, (unsigned)received.month, (unsigned)received.day,」 补足上行的表达式、缓冲区内容或接口参数。 */
          (unsigned)received.hour, (unsigned)received.minute, (unsigned)received.second); /* 按无符号数格式传入应答的时分秒字段，便于核对本次同步。 */
}

uint8_t RTC_Screen_GetTime(RTC_Time_t *time) /* 向调用方提供已同步的时间副本，未同步或指针为空时报告失败。 */
{
    /* 第一次校时前，调用方收到 0，输出缓冲区也不被改写。成功时先推进到当前秒。 */
    if (!time_valid || time == 0) /* 还没同步到有效时间或没有输出地址时，禁止复制时间结构。 */
        return 0U; /* 以 0U 返回本分支的结果，调用方据此处理当前状态。 */

    advance_clock(delay_millis()); /* 按已过完整秒推进缓存时间，保留不足一秒的节拍差。 */
    *time = screen_time; /* 将时间 更新为 当前屏幕时间缓存。 */
    return 1U; /* 将 「1U」 返回给调用者，供上层使用。 */
}
