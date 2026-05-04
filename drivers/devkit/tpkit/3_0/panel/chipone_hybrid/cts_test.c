#define LOG_TAG         "Test"

#include "cts_config.h"
#include "cts_platform.h"
#include "cts_core.h"
#include "cts_strerror.h"
#include "cts_test.h"

bool print_test_touch_data_to_console = false;
module_param_named(print_test_touch_data_to_console,
	print_test_touch_data_to_console, bool, 0660);
const char *cts_test_item_str(int test_item)
{
#define case_test_item(item) \
	case CTS_TEST_ ## item: return #item "-TEST"

	switch (test_item) {
	case_test_item(RESET_PIN);
	case_test_item(INT_PIN);
	case_test_item(RAWDATA);
	case_test_item(DEVIATION);
	case_test_item(NOISE);
	case_test_item(OPEN);
	case_test_item(SHORT);
	default:
		return "INVALID";
	}
#undef case_test_item
}

#define CTS_FIRMWARE_WORK_MODE_NORMAL   0x00
#define CTS_FIRMWARE_WORK_MODE_FACTORY  0x01
#define CTS_FIRMWARE_WORK_MODE_CONFIG   0x02
#define CTS_FIRMWARE_WORK_MODE_TEST     0x03

#define CTS_TEST_SHORT                  0x01
#define CTS_TEST_OPEN                   0x02

#define CTS_SHORT_TEST_UNDEFINED        0x00
#define CTS_SHORT_TEST_BETWEEN_COLS     0x01
#define CTS_SHORT_TEST_BETWEEN_ROWS     0x02
#define CTS_SHORT_TEST_BETWEEN_GND      0x03

#define TEST_RESULT_BUFFER_SIZE(cts_dev) \
	((cts_dev->fwdata.rows) * (cts_dev->fwdata.cols) * (2))

#define RAWDATA_BUFFER_SIZE(cts_dev) \
	((cts_dev->fwdata.rows) * (cts_dev->fwdata.cols) * (2))

static int freq_hop_off(struct cts_device *cts_dev)
{
	int ret;

	ret = cts_send_command(cts_dev, CTS_CMD_FREQ_HOP_OFF);
	if (ret)
		TS_LOG_ERR("Send command CTS_CMD_MONITOR_OFF failed %d", ret);

	return ret;
}

static int disable_fw_monitor_mode(struct cts_device *cts_dev)
{
	int ret;

	ret = cts_send_command(cts_dev, CTS_CMD_MONITOR_OFF);
	if (ret)
		TS_LOG_ERR("Send command CTS_CMD_MONITOR_OFF failed %d", ret);

	return ret;
}

static int disable_fw_low_power(struct cts_device *cts_dev)
{
	int ret;

	ret = cts_send_command(cts_dev, CTS_CMD_LOW_POWER_OFF);
	if (ret)
		TS_LOG_ERR("Send command CTS_CMD_LOW_POWER_OFF failed %d", ret);

	return ret;
}

static int disable_fw_gesture_mode(struct cts_device *cts_dev)
{
	int ret;

	ret = cts_send_command(cts_dev, CTS_CMD_QUIT_GESTURE_MONITOR);
	if (ret)
		TS_LOG_ERR(
		"Send command CTS_CMD_QUIT_GESTURE_MONITOR failed %d", ret);

	return ret;
}

int cts_write_file(struct file *filp, const void *data, size_t size)
{
	loff_t pos;
	ssize_t ret;

	pos = filp->f_pos;
	ret = kernel_write(filp, data, size, &pos);
	if (ret >= 0)
		filp->f_pos += ret;
	return ret;
}

/* Make directory for filepath
 * If filepath = "/A/B/C/D.file", it will make dir /A/B/C recursive
 * like userspace mkdir -p
 */
int cts_mkdir_for_file(const char *filepath, umode_t mode)
{
	char *dirname = NULL;
	int dirname_len;
	char *s = NULL;
	int ret;
	mm_segment_t fs;

	if (filepath == NULL) {
		TS_LOG_ERR("Create dir for file with filepath = NULL");
		return -EINVAL;
	}

	if (filepath[0] == '\0' || filepath[0] != '/') {
		TS_LOG_ERR("Create dir for file with invalid filepath[0]: %c",
			   filepath[0]);
		return -EINVAL;
	}

	dirname_len = strrchr(filepath, '/') - filepath;
	if (dirname_len == 0) {
		TS_LOG_INFO("Create dir for file '%s' in root dir", filepath);
		return 0;
	}

	dirname = kstrndup(filepath, dirname_len, GFP_KERNEL);
	if (dirname == NULL) {
		TS_LOG_ERR("Create dir alloc mem for dirname failed");
		return -ENOMEM;
	}

	TS_LOG_INFO("Create dir '%s' for file '%s'", dirname, filepath);

	fs = get_fs();
	set_fs(KERNEL_DS);

	s = dirname + 1; /* Skip leading '/' */
	while (1) {
		char c = '\0';
		/* Bypass leading non-'/'s and then subsequent '/'s */
		while (*s) {
			if (*s == '/') {
				do
					++s;
				while (*s == '/');
				c = *s; /* Save current char */
				*s = '\0'; /* and replace it with nul */
				break;
			}
			++s;
		}
		TS_LOG_DEBUG(" - Create dir '%s'", dirname);
		ret = ksys_mkdir(dirname, 0777);
		if (ret < 0 && ret != -EEXIST) {
			TS_LOG_INFO("Create dir '%s' failed %d(%s)",
				dirname, ret, cts_strerror(ret));
				*s = c;
				break;
		}
		/* Reset ret to 0 if return -EEXIST */
		ret = 0;

		if (c)
			*s = c;
		else
			break;
	}

	set_fs(fs);

	if (dirname != NULL)
		kfree(dirname);

	return ret;
}

struct file *cts_test_data_filp = NULL;
int cts_start_dump_test_data_to_file(const char *filepath, bool append_to_file)
{
	int ret;

#define START_BANNER \
		">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n"

	TS_LOG_INFO("Start dump test data to file '%s'", filepath);

	ret = cts_mkdir_for_file(filepath, 0777);
	if (ret) {
		TS_LOG_ERR("Create dir for test data file failed %d", ret);
		return ret;
	}

	cts_test_data_filp = filp_open(filepath,
		O_WRONLY | O_CREAT | (append_to_file ? O_APPEND : O_TRUNC), 0660);
	if (IS_ERR(cts_test_data_filp)) {
		ret = PTR_ERR(cts_test_data_filp);
		cts_test_data_filp = NULL;
		TS_LOG_ERR("Open file '%s' for test data failed %d",
			   cts_test_data_filp, ret);
		return ret;
	}

	cts_write_file(cts_test_data_filp, START_BANNER, strlen(START_BANNER));

	return 0;
#undef START_BANNER
}

void cts_stop_dump_test_data_to_file(void)
{
#define END_BANNER \
"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n"
	int r;

	TS_LOG_INFO("Stop dump test data to file");

	if (cts_test_data_filp) {
		cts_write_file(cts_test_data_filp,
			       END_BANNER, strlen(END_BANNER));
		r = filp_close(cts_test_data_filp, NULL);
		if (r)
			TS_LOG_ERR("Close test data file failed %d", r);
		cts_test_data_filp = NULL;
	} else {
		TS_LOG_INFO("Stop dump tsdata to file with filp = NULL");
	}
#undef END_BANNER
}

static void cts_dump_tsdata(struct cts_device *cts_dev,
	const char *desc, void *origdata, bool to_console)
{
#define SPLIT_LINE_STR \
"---------------------------------------------------------------------------------------------------------------"
#define ROW_NUM_FORMAT_STR  "%2d | "
#define COL_NUM_FORMAT_STR  "%-5u "
#define DATA_FORMAT_STR     "%-5u "
	int r, c;
	u32 max, min, sum, average;
	int max_r, max_c, min_r, min_c;
	char line_buf[128];
	int count = 0;
	int rows = cts_dev->fwdata.rows;
	int cols = cts_dev->fwdata.cols;
	u32 val;
	s32 *data_s32 = NULL;
	u16 *data = NULL;
	bool is_short_test = false;

	if (!rows || !cols) {
		TS_LOG_ERR("%s: parameters invalid !\n", __func__);
		return;
	}
	to_console = to_console || print_test_touch_data_to_console;

	if (strstr(desc, "Short") != NULL || strstr(desc, "SCap") != NULL) {
		rows = 1;
		cols = cts_dev->fwdata.cols + cts_dev->fwdata.rows;
	}
	if (strstr(desc, "col") != NULL )
		cols -= 1;
	if (strstr(desc, "row") != NULL)
		rows -= 1;
	if (strstr(desc, "Short") != NULL) {
		data_s32 = (s32 *)origdata;
		is_short_test = true;
		max = data_s32[0];
		min = data_s32[0];
	} else {
		data = (u16 *)origdata;
		max = data[0];
		min = data[0];
	}

	sum = 0;
	val = 0;
	max_r = max_c = min_r = min_c = 0;
	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			if (is_short_test)
				val = data_s32[r * cols + c];
			else
				val = data[r * cols + c];

			sum += val;
			if (val > max) {
				max = val;
				max_r = r;
				max_c = c;
			} else if (val < min) {
				min = val;
				min_r = r;
				min_c = c;
			}
		}
	}
	average = sum / (rows * cols);

	count = 0;
	count += scnprintf(line_buf + count, sizeof(line_buf) - count,
			   " %s test data MIN: [%u][%u]=%u, MAX: [%u][%u]=%u, AVG=%u",
			   desc, min_r, min_c, min, max_r, max_c, max, average);

	if (to_console) {
		TS_LOG_DEBUG(SPLIT_LINE_STR);
		TS_LOG_INFO("%s", line_buf);
		TS_LOG_DEBUG(SPLIT_LINE_STR);
	}
	if (cts_test_data_filp) {
		cts_write_file(cts_test_data_filp, SPLIT_LINE_STR,
			       strlen(SPLIT_LINE_STR));
		cts_write_file(cts_test_data_filp, "\n", 1);
		cts_write_file(cts_test_data_filp, line_buf, count);
		cts_write_file(cts_test_data_filp, "\n", 1);
		cts_write_file(cts_test_data_filp, SPLIT_LINE_STR,
			       strlen(SPLIT_LINE_STR));
		cts_write_file(cts_test_data_filp, "\n", 1);
	}

	count = 0;
	count +=
	    scnprintf(line_buf + count, sizeof(line_buf) - count, "   |  ");
	for (c = 0; c < cols; c++)
		count += scnprintf(line_buf + count, sizeof(line_buf) - count,
			COL_NUM_FORMAT_STR, c);
	if (to_console) {
		TS_LOG_DEBUG("%s", line_buf);
		TS_LOG_DEBUG(SPLIT_LINE_STR);
	}
	if (cts_test_data_filp) {
		cts_write_file(cts_test_data_filp, line_buf, count);
		cts_write_file(cts_test_data_filp, "\n", 1);
		cts_write_file(cts_test_data_filp, SPLIT_LINE_STR,
			       strlen(SPLIT_LINE_STR));
		cts_write_file(cts_test_data_filp, "\n", 1);
	}

	for (r = 0; r < rows; r++) {
		count = 0;
		count += scnprintf(line_buf + count, sizeof(line_buf) - count,
				   ROW_NUM_FORMAT_STR, r);
		for (c = 0; c < cols; c++) {
			if (is_short_test) {
				count += scnprintf(line_buf + count,
					sizeof(line_buf) - count, DATA_FORMAT_STR,
					data_s32[r * cols + c]);
			} else {
				count += scnprintf(line_buf + count,
					sizeof(line_buf) - count, DATA_FORMAT_STR,
					data[r * cols + c]);
			}
		}
		if (to_console)
			TS_LOG_DEBUG("%s", line_buf);

		if (cts_test_data_filp) {
			cts_write_file(cts_test_data_filp, line_buf, count);
			cts_write_file(cts_test_data_filp, "\n", 1);
		}
	}
	if (to_console)
		TS_LOG_DEBUG(SPLIT_LINE_STR);

	if (cts_test_data_filp) {
		cts_write_file(cts_test_data_filp, SPLIT_LINE_STR,
			       strlen(SPLIT_LINE_STR));
		cts_write_file(cts_test_data_filp, "\n", 1);
	}
#undef SPLIT_LINE_STR
#undef ROW_NUM_FORMAT_STR
#undef COL_NUM_FORMAT_STR
#undef DATA_FORMAT_STR
}

static bool is_invalid_node(u32 *invalid_nodes, u32 num_invalid_nodes,
			    u16 row, u16 col)
{
	int i;

	for (i = 0; i < num_invalid_nodes; i++) {
		if (MAKE_INVALID_NODE(row, col) == invalid_nodes[i])
			return true;
	}

	return false;
}

static int validate_tsdata(struct cts_device *cts_dev,
			   const char *desc, void *origdata,
			   u32 *invalid_nodes, u32 num_invalid_nodes,
			   bool per_node, int *min, int *max)
{
#define SPLIT_LINE_STR \
	"------------------------------"

	int r, c;
	int failed_cnt = 0;
	int rows = cts_dev->fwdata.rows;
	int cols = cts_dev->fwdata.cols;
	s32 *data_s32 = NULL;
	u16 *data = NULL;
	bool is_short_test = false;
	int offset;

	if (!rows || !cols) {
		TS_LOG_ERR("%s: parameters invalid !\n", __func__);
		return -EINVAL;
	}
	if (strstr(desc, "Short") != NULL || strstr(desc, "SCap") != NULL) {
		rows = 1;
		cols = cts_dev->fwdata.cols + cts_dev->fwdata.rows;
	}
	if(strstr(desc, "Short") != NULL){
		data_s32 = (s32 *)origdata;
		is_short_test = true;
	} else {
		data = (u16 *)origdata;
	}

	TS_LOG_INFO
	    ("%s validate data: %s, num invalid node: %u, thresh[0]=[%d, %d]",
	     desc, per_node ? "Per-Node" : "Uniform-Threshold",
	     num_invalid_nodes, min ? min[0] : INT_MIN, max ? max[0] : INT_MAX);

	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			offset = r * cols + c;

			if (num_invalid_nodes &&
			    is_invalid_node(invalid_nodes, num_invalid_nodes, r, c))
				continue;
			if (is_short_test) {
				if ((min != NULL&& data_s32[offset] <min[per_node ? offset : 0]) ||
					(max != NULL&&data_s32[offset] >max[per_node ?offset : 0])) {
					if (failed_cnt == 0) {
						TS_LOG_INFO(SPLIT_LINE_STR);
						TS_LOG_INFO("%s failed nodes:", desc);
					}
					failed_cnt++;
					TS_LOG_INFO("  %3d: [%-2d][%-2d] = %d,",failed_cnt, r, c,data_s32[offset]);
				}
			} else {
				if ((min != NULL&& data[offset] <min[per_node ? offset : 0]) ||
					(max != NULL&&data[offset] >max[per_node ?offset : 0])) {
					if (failed_cnt == 0) {
						TS_LOG_INFO(SPLIT_LINE_STR);
						TS_LOG_INFO("%s failed nodes:", desc);
					}
					failed_cnt++;
					TS_LOG_INFO("  %3d: [%-2d][%-2d] = %u,",failed_cnt, r, c,data[offset]);
				}
			}
		}
	}

	if (failed_cnt) {
		TS_LOG_INFO(SPLIT_LINE_STR);
		TS_LOG_INFO("%s test %d node total failed", desc, failed_cnt);
	}

	return failed_cnt;

#undef SPLIT_LINE_STR
}

static int prepare_test(struct cts_device *cts_dev)
{
	int ret;

	TS_LOG_INFO("Prepare test");
	ret = disable_fw_monitor_mode(cts_dev);
	if (ret) {
		TS_LOG_ERR("Disable firmware monitor mode failed %d", ret);
		return ret;
	}

	ret = disable_fw_gesture_mode(cts_dev);
	if (ret) {
		TS_LOG_ERR("Disable firmware monitor mode failed %d", ret);
		return ret;
	}

	ret = disable_fw_low_power(cts_dev);
	if (ret) {
		TS_LOG_ERR("Disable firmware low power failed %d", ret);
		return ret;
	}

	ret = freq_hop_off(cts_dev);
	if (ret) {
		TS_LOG_ERR("freq_hop_off failed %d", ret);
		return ret;
	}
	cts_dev->rtdata.testing = true;

	return 0;
}

static void post_test(struct cts_device *cts_dev)
{
	TS_LOG_INFO("Post test");

	cts_plat_reset_device(cts_dev->pdata);
	cts_dev->rtdata.testing = false;
}
int cts_test_short(struct cts_device *cts_dev, struct cts_test_param *param)
{
	bool driver_validate_data = false;
	bool validate_data_per_node = false;
	bool stop_if_failed = false;
	bool dump_test_data_to_user = false;
	bool dump_test_data_to_console = false;
	bool dump_test_data_to_file = false;
	int num_nodes;
	int tsdata_frame_size;
	int ret = 0;
	int i;
	int r;
	s32 *test_data = NULL;
	ktime_t start_time, end_time, delta_time;
	struct chipone_ts_data *cts_data =
		container_of(cts_dev, struct chipone_ts_data, cts_dev);

	if (cts_dev == NULL || param == NULL) {
		TS_LOG_ERR
		    ("Short test with invalid param: cts_dev: %p test param: %p",
		     cts_dev, param);
		return -EINVAL;
	}

	num_nodes = cts_dev->fwdata.rows + cts_dev->fwdata.cols;
	tsdata_frame_size = 4 * num_nodes;

	driver_validate_data = !!(param->flags & CTS_TEST_FLAG_VALIDATE_DATA);
	validate_data_per_node =
	    !!(param->flags & CTS_TEST_FLAG_VALIDATE_PER_NODE);
	dump_test_data_to_user =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_USERSPACE);
	dump_test_data_to_console =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_CONSOLE);
	dump_test_data_to_file =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE);
	stop_if_failed =
	    !!(param->flags & CTS_TEST_FLAG_STOP_TEST_IF_VALIDATE_FAILED);

	TS_LOG_INFO("Short test, flags: 0x%08x, num invalid node: %u, "
		    "test data file: '%s' buf size: %d, drive log file: '%s' buf size: %d",
		    param->flags, param->num_invalid_node,
		    param->test_data_filepath, param->test_data_buf_size,
		    param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	if (dump_test_data_to_user) {
		test_data = (s32 *)param->test_data_buf;
	} else {
		test_data = kmalloc(tsdata_frame_size, GFP_KERNEL);
		if (test_data == NULL) {
			TS_LOG_ERR("Allocate test result buffer failed");
			ret = -ENOMEM;
			strncpy(cts_data->failedreason, "-software_reason",
				TS_RAWDATA_FAILED_REASON_LEN - 1);
			goto show_test_result;
		}
	}

	ret = cts_stop_device(cts_dev);
	if (ret) {
		TS_LOG_ERR("Stop device failed %d", ret);
		goto free_mem;
	}

	cts_lock_device(cts_dev);

	ret = prepare_test(cts_dev);
	if (ret) {
		TS_LOG_ERR("Prepare test failed %d", ret);
		goto post_test;
	}

	for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
		r = cts_start_fw_short_opentest(cts_dev);
		if (r) {
			TS_LOG_ERR("Enable get shortdata failed %d", r);
			continue;
		} else {
			break;
		}
	}

	if (i >= CFG_CTS_GET_DATA_RETRY) {
		TS_LOG_ERR("Enable read short test data failed");
		ret = -EIO;
		strncpy(cts_data->failedreason, "-panel_reason",
			TS_RAWDATA_FAILED_REASON_LEN - 1);
		goto post_test;
	}

	for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
		ret = cts_get_shortdata(cts_dev, test_data);
		if (ret) {
			TS_LOG_ERR("Get short test data failed %d", ret);
			mdelay(30);
		} else {
			if (dump_test_data_to_user)
				*param->test_data_wr_size += tsdata_frame_size;
			break;
		}
	}
	if (i >= CFG_CTS_GET_DATA_RETRY) {
		TS_LOG_ERR("Get short test data failed %d", ret);
		goto post_test;
	}
	if (dump_test_data_to_file) {
		r = cts_start_dump_test_data_to_file(param->test_data_filepath,
			!!(param->flags &
			 CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE_APPEND));
		if (r)
			TS_LOG_ERR("Start short test data to file failed %d", r);
	}

	if (dump_test_data_to_console || dump_test_data_to_file)
		cts_dump_tsdata(cts_dev, "Short data", test_data,
			dump_test_data_to_console);

	if (dump_test_data_to_file)
		cts_stop_dump_test_data_to_file();

	if (driver_validate_data) {
		ret = validate_tsdata(cts_dev, "Short data", test_data,
			param->invalid_nodes, param->num_invalid_node,
			validate_data_per_node, param->min, NULL);
		if (ret) {
			TS_LOG_ERR("Short data test failed %d", ret);
			strncpy(cts_data->failedreason, "-panel_reason",
				TS_RAWDATA_FAILED_REASON_LEN - 1);
		}
	}
post_test:
	post_test(cts_dev);
	cts_unlock_device(cts_dev);
	cts_start_device(cts_dev);

free_mem:
	if (!dump_test_data_to_user && test_data)
		kfree(test_data);

show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	if (ret > 0) {
		TS_LOG_INFO
		    ("Short test has %d nodes FAIL, ELAPSED TIME: %lldms", ret,
		     ktime_to_ms(delta_time));
	} else if (ret < 0) {
		TS_LOG_INFO("Short test FAIL %d(%s), ELAPSED TIME: %lldms",
			    ret, cts_strerror(ret), ktime_to_ms(delta_time));
	} else {
		TS_LOG_INFO("Short test PASS, ELAPSED TIME: %lldms",
			    ktime_to_ms(delta_time));
	}

	return ret;
}

int cts_test_open(struct cts_device *cts_dev,
	struct cts_test_param *param)
{
	bool driver_validate_data = false;
	bool validate_data_per_node = false;
	bool stop_if_failed = false;
	bool dump_test_data_to_user = false;
	bool dump_test_data_to_console = false;
	bool dump_test_data_to_file = false;
	int num_nodes;
	int tsdata_frame_size;
	int ret = 0;
	int i;
	int r;
	u16 *test_data = NULL;
	ktime_t start_time, end_time, delta_time;
	struct chipone_ts_data *cts_data =
		container_of(cts_dev, struct chipone_ts_data, cts_dev);

	if (cts_dev == NULL || param == NULL) {
		TS_LOG_ERR
		    ("open drive test with invalid param: cts_dev: %p test param: %p",
		     cts_dev, param);
		return -EINVAL;
	}

	num_nodes = cts_dev->fwdata.rows * cts_dev->fwdata.cols;
	tsdata_frame_size = 2 * num_nodes;

	driver_validate_data = !!(param->flags & CTS_TEST_FLAG_VALIDATE_DATA);
	validate_data_per_node =
	    !!(param->flags & CTS_TEST_FLAG_VALIDATE_PER_NODE);
	dump_test_data_to_user =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_USERSPACE);
	dump_test_data_to_console =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_CONSOLE);
	dump_test_data_to_file =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE);
	stop_if_failed =
	    !!(param->flags & CTS_TEST_FLAG_STOP_TEST_IF_VALIDATE_FAILED);

	TS_LOG_INFO("Open drive test, flags: 0x%08x,"
		    "num invalid node: %u, "
		    "test data file: '%s' buf size: %d, "
		    "drive log file: '%s' buf size: %d",
		    param->flags, param->num_invalid_node,
		    param->test_data_filepath, param->test_data_buf_size,
		    param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	if (dump_test_data_to_user) {
		test_data = (u16 *)param->test_data_buf;
	} else {
		test_data = (u16 *)kmalloc(tsdata_frame_size, GFP_KERNEL);
		if (test_data == NULL) {
			TS_LOG_ERR("Allocate test result buffer failed");
			ret = -ENOMEM;
			strncpy(cts_data->failedreason, "-software_reason",
				TS_RAWDATA_FAILED_REASON_LEN - 1);
			goto show_test_result;
		}
	}

	ret = cts_stop_device(cts_dev);
	if (ret) {
		TS_LOG_ERR("Stop device failed %d", ret);
		goto free_mem;
	}

	cts_lock_device(cts_dev);

	ret = prepare_test(cts_dev);
	if (ret) {
		TS_LOG_ERR("Prepare test failed %d", ret);
		goto post_test;
	}

	for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
		r = cts_start_fw_short_opentest(cts_dev);
		if (r) {
			TS_LOG_ERR("Start open test failed %d", r);
			continue;
		} else {
			TS_LOG_DEBUG("Enable short and open test susccess ");
			break;
		}
	}

	if (i >= CFG_CTS_GET_DATA_RETRY) {
		TS_LOG_ERR("Enable short and open test failed");
		ret = -EIO;
		strncpy(cts_data->failedreason, "-panel_reason",
			TS_RAWDATA_FAILED_REASON_LEN - 1);
		goto post_test;
	}

	for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
		r = cts_get_opendata(cts_dev, test_data);
		if (r) {
			TS_LOG_ERR("Get open test data failed %d", r);
			mdelay(30);
		} else {
			if (dump_test_data_to_user)
				*param->test_data_wr_size += tsdata_frame_size;
			break;
		}
	}
	if (i >= CFG_CTS_GET_DATA_RETRY) {
		TS_LOG_ERR("Get open test data failed %d", ret);
		strncpy(cts_data->failedreason, "-software_reason",
			TS_RAWDATA_FAILED_REASON_LEN - 1);
		goto post_test;
	}

	if (dump_test_data_to_file) {
		r = cts_start_dump_test_data_to_file(param->test_data_filepath,
			!!(param->flags &
			CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE_APPEND));
		if (r)
			TS_LOG_ERR("Start open data test data to file failed %d", r);
	}

	if (dump_test_data_to_console || dump_test_data_to_file)
		cts_dump_tsdata(cts_dev, "open data", test_data,
			dump_test_data_to_console);

	if (dump_test_data_to_file)
		cts_stop_dump_test_data_to_file();

	if (driver_validate_data) {
		ret = validate_tsdata(cts_dev, "open data", test_data,
			param->invalid_nodes, param->num_invalid_node,
			validate_data_per_node, param->min, param->max);
		if (ret) {
			TS_LOG_ERR("open data test failed %d", ret);
			strncpy(cts_data->failedreason, "-panel_reason",
				TS_RAWDATA_FAILED_REASON_LEN - 1);
		}
	}
post_test:
	post_test(cts_dev);
	cts_unlock_device(cts_dev);
	cts_start_device(cts_dev);

free_mem:
	if (!dump_test_data_to_user && test_data)
		kfree(test_data);

show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	if (ret > 0) {
		TS_LOG_INFO
		    ("Open test has %d nodes FAIL, ELAPSED TIME: %lldms", ret,
		     ktime_to_ms(delta_time));
	} else if (ret < 0) {
		TS_LOG_INFO("Open test FAIL %d(%s), ELAPSED TIME: %lldms",
			    ret, cts_strerror(ret), ktime_to_ms(delta_time));
	} else {
		TS_LOG_INFO("Open test PASS, ELAPSED TIME: %lldms",
			    ktime_to_ms(delta_time));
	}

	return ret;
}

int cts_test_reset_pin(struct cts_device *cts_dev, struct cts_test_param *param)
{
	ktime_t start_time, end_time, delta_time;
	int ret = 0;
	int r;
	int i = 0;
	struct chipone_ts_data *cts_data =
		container_of(cts_dev, struct chipone_ts_data, cts_dev);

	if (cts_dev == NULL || param == NULL) {
		TS_LOG_ERR
		    ("Reset-pin test with invalid param: cts_dev: %p test param: %p",
		     cts_dev, param);
		return -EINVAL;
	}

	TS_LOG_INFO("Reset-Pin test, flags: 0x%08x, drive log file: '%s' buf size: %d",
		    param->flags, param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	ret = cts_stop_device(cts_dev);
	if (ret) {
		TS_LOG_ERR("Stop device failed %d", ret);
		goto show_test_result;
	}

	cts_lock_device(cts_dev);

	cts_plat_set_reset(cts_dev->pdata, 0);
	mdelay(50);
	TS_LOG_INFO("gpio_get_value:%d\n",
		gpio_get_value(cts_data->tskit_data->ts_platform_data->reset_gpio));

	for (i = 0; i < 3; i++) {
		/* Check whether device is in normal mode */
		if (cts_plat_is_i2c_online(cts_dev->pdata, CTS_DEV_NORMAL_MODE_I2CADDR)) {
			ret = -EIO;
			TS_LOG_ERR("Device is alive while reset is low");
		} else {
			ret = 0;
			TS_LOG_ERR("reset active.\n");
			break;
		}
	}
	TS_LOG_INFO("retry cnt is %d\n",i);

	cts_plat_set_reset(cts_dev->pdata, 1);
	mdelay(50);

	/* Check whether device is in normal mode */
	if (!cts_plat_is_i2c_online(cts_dev->pdata,
				    CTS_DEV_NORMAL_MODE_I2CADDR)) {
		ret = -EIO;
		strncpy(cts_data->failedreason, "-software_reason",
			TS_RAWDATA_FAILED_REASON_LEN - 1);
		TS_LOG_ERR("Device is offline while reset is high");
	}

	cts_unlock_device(cts_dev);
	r = cts_start_device(cts_dev);
	if (r)
		TS_LOG_ERR("Start device failed %d", r);
	if (!cts_dev->rtdata.program_mode)
		cts_set_normal_addr(cts_dev);

show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	if (ret) {
		TS_LOG_INFO("Reset-Pin test FAIL %d(%s), ELAPSED TIME: %lldms",
			    ret, cts_strerror(ret), ktime_to_ms(delta_time));
	} else {
		TS_LOG_INFO("Reset-Pin test PASS, ELAPSED TIME: %lldms",
			    ktime_to_ms(delta_time));
	}

	return ret;
}

int cts_test_int_pin(struct cts_device *cts_dev, struct cts_test_param *param)
{
	ktime_t start_time, end_time, delta_time;
	int ret = 0;
	int r;
	struct chipone_ts_data *cts_data =
		container_of(cts_dev, struct chipone_ts_data, cts_dev);

	if (cts_dev == NULL || param == NULL) {
		TS_LOG_ERR
		    ("Int-pin test with invalid param: cts_dev: %p test param: %p",
		     cts_dev, param);
		return -EINVAL;
	}

	TS_LOG_INFO("Int-Pin test, flags: 0x%08x, "
		    "drive log file: '%s' buf size: %d",
		    param->flags,
		    param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	ret = cts_stop_device(cts_dev);
	if (ret) {
		TS_LOG_ERR("Stop device failed %d", ret);
		goto show_test_result;
	}

	cts_lock_device(cts_dev);

	ret = cts_send_command(cts_dev, CTS_CMD_WRTITE_INT_HIGH);
	if (ret) {
		TS_LOG_ERR("Send command WRTITE_INT_HIGH failed %d", ret);
		goto unlock_device;
	}
	mdelay(10);
	if (cts_plat_get_int_pin(cts_dev->pdata) == 0) {
		TS_LOG_ERR("INT pin state != HIGH");
		ret = -EFAULT;
		strncpy(cts_data->failedreason, "-panel_reason",
			TS_RAWDATA_FAILED_REASON_LEN - 1);
		goto exit_int_test;
	}

	ret = cts_send_command(cts_dev, CTS_CMD_WRTITE_INT_LOW);
	if (ret) {
		TS_LOG_ERR("Send command WRTITE_INT_LOW failed %d", ret);
		strncpy(cts_data->failedreason, "-software_reason",
			TS_RAWDATA_FAILED_REASON_LEN - 1);
		goto exit_int_test;
	}
	mdelay(10);
	if (cts_plat_get_int_pin(cts_dev->pdata) != 0) {
		TS_LOG_ERR("INT pin state != LOW");
		ret = -EFAULT;
		strncpy(cts_data->failedreason, "-panel_reason",
			TS_RAWDATA_FAILED_REASON_LEN - 1);
		goto exit_int_test;
	}

exit_int_test:
	r = cts_send_command(cts_dev, CTS_CMD_RELASE_INT_TEST);
	if (r)
		TS_LOG_ERR("Send command RELASE_INT_TEST failed %d", r);
	mdelay(10);

unlock_device:
	cts_unlock_device(cts_dev);
	r = cts_start_device(cts_dev);
	if (r)
		TS_LOG_ERR("Start device failed %d", r);

show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	if (ret) {
		TS_LOG_INFO("Int-Pin test FAIL %d(%s), ELAPSED TIME: %lldms",
			    ret, cts_strerror(ret), ktime_to_ms(delta_time));
	} else {
		TS_LOG_INFO("Int-Pin test PASS, ELAPSED TIME: %lldms",
			    ktime_to_ms(delta_time));
	}

	return ret;
}

static int cts_deviation_calc(const char *desc, u16 *data, u8 rows, u8 cols,u16 *result)
{
	u32 notes;
	int i;
	int j;
	int offset;
	int is_col_data = 0;
	int is_row_data = 0;

	notes = rows * cols;

	if (cols == 0 || notes == 0)
		return -EINVAL;

	if (strstr(desc,"row") != NULL) {
		is_row_data = 1;
	} else if (strstr(desc,"col") != NULL) {
		is_col_data = 1;
	}


	for (i = 0; i < notes; i++) {
		if (data[i] == 0) {
			TS_LOG_ERR("%s:%d rawdata error,data[%d] = %d,NG\n",
				__func__,__LINE__,i,data[i]);
			return -EINVAL;
		}
	}

	if (is_col_data) {
		for (i = 0; i < rows; i++) {
			for (j = 0; j < cols-1; j++) {
				offset = i*cols + j;
				result[i*(cols-1)+j] = abs(data[offset+1] - data[offset]);
			}
		}
	} else if (is_row_data) {
		for (i = 0; i < rows - 1; i++) {
			for (j = 0; j < cols; j++) {
				offset = i * cols + j;
				result[i * cols + j] = abs(data[offset + cols] - data[offset]);
			}
		}
	}

	return 0;
}

int cts_test_deviation(struct cts_device *cts_dev, struct cts_test_param *param)
{
	bool driver_validate_data = false;
	bool validate_data_per_node = false;
	bool stop_test_if_validate_fail = false;
	bool dump_test_data_to_user = false;
	bool dump_test_data_to_console = false;
	bool dump_test_data_to_file = false;
	int num_nodes;
	int row_failed_nodes = 0;
	int col_failed_nodes = 0;
	int mcap_nodes, scap_nodes;
	int tsdata_frame_size;
	u16 *deviation_col = NULL;
	u16 *deviation_row = NULL;
	u16 *rawdata =NULL;
	ktime_t start_time, end_time, delta_time;
	int ret = 0;
	int r;
	struct chipone_ts_data *cts_data = container_of(cts_dev, struct chipone_ts_data, cts_dev);

	if (cts_dev == NULL || param == NULL) {
		TS_LOG_ERR("Rawdata test with invalid param");
		return -EINVAL;
	}

	mcap_nodes = cts_dev->fwdata.rows * cts_dev->fwdata.cols;
	scap_nodes = cts_dev->fwdata.rows + cts_dev->fwdata.cols;
	num_nodes = mcap_nodes + scap_nodes;
	tsdata_frame_size = 2 * num_nodes;

	driver_validate_data = !!(param->flags & CTS_TEST_FLAG_VALIDATE_DATA);
	validate_data_per_node =
		!!(param->flags & CTS_TEST_FLAG_VALIDATE_PER_NODE);
	dump_test_data_to_user =
		!!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_USERSPACE);
	dump_test_data_to_console =
		!!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_CONSOLE);
	dump_test_data_to_file =
		!!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE);
	stop_test_if_validate_fail =
		!!(param->flags & CTS_TEST_FLAG_STOP_TEST_IF_VALIDATE_FAILED);

	TS_LOG_INFO("Deviation test, flags: 0x%08x,"
		"num invalid node: %u, "
		"test data file: '%s' buf size: %d, "
		"drive log file: '%s' buf size: %d",
		param->flags, param->num_invalid_node,
		param->test_data_filepath, param->test_data_buf_size,
		param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	rawdata = (u16 *)cts_data->rawdata_test_param.test_data_buf;
	if (rawdata == NULL) {
		TS_LOG_ERR("Allocate memory for Deviation failed");
		ret = -ENOMEM;
		strncpy(cts_data->failedreason, "-software_reason",
			TS_RAWDATA_FAILED_REASON_LEN -1);
		goto show_test_result;
	}

	deviation_col = (u16 *)kmalloc(tsdata_frame_size, GFP_KERNEL);
	if (deviation_col == NULL) {
		TS_LOG_ERR("Allocate memory for Deviation failed");
		ret = -ENOMEM;
		goto show_test_result;
	}
	deviation_row = (u16 *)kmalloc(tsdata_frame_size, GFP_KERNEL);
	if (deviation_row == NULL) {
		TS_LOG_ERR("Allocate memory for Deviation failed");
		ret = -ENOMEM;
		goto free_mem;
	}

	if (dump_test_data_to_file) {
		r = cts_start_dump_test_data_to_file(param->test_data_filepath,
			!!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE_APPEND));
		if (r)
			TS_LOG_ERR("Start dump test data to file failed %d", r);
	}

	if (cts_deviation_calc("col", rawdata, cts_dev->fwdata.rows,
		cts_dev->fwdata.cols, deviation_col) != 0) {
		ret = -EINVAL;
		TS_LOG_ERR("calc mutual deviation failed %d", ret);
	} else {
		if (dump_test_data_to_console || dump_test_data_to_file)
			cts_dump_tsdata(cts_dev, "col", deviation_col,
				dump_test_data_to_console);

		if (driver_validate_data) {
			col_failed_nodes = validate_tsdata(cts_dev,
				"col", deviation_col,
				param->invalid_nodes,
				param->num_invalid_node,
				validate_data_per_node,
				NULL,
				param->max_col);
			if (col_failed_nodes) {
				TS_LOG_ERR("Mutual deviation test failed %d", col_failed_nodes);
				if (stop_test_if_validate_fail)
					strncpy(cts_data->failedreason, "-panel_reason",
                        				TS_RAWDATA_FAILED_REASON_LEN - 1);
			}
		}
	}
	if (cts_deviation_calc("row", rawdata, cts_dev->fwdata.rows,
		cts_dev->fwdata.cols, deviation_row) != 0) {
		ret = -EINVAL;
		TS_LOG_ERR("calc mutual deviation failed %d", ret);
	} else {
		if (dump_test_data_to_console || dump_test_data_to_file)
			cts_dump_tsdata(cts_dev, "row", deviation_row,
				dump_test_data_to_console);

		if (driver_validate_data) {
			row_failed_nodes = validate_tsdata(cts_dev,
				"row", deviation_row,
				param->invalid_nodes,
				param->num_invalid_node,
				validate_data_per_node,
				NULL,
				param->max_row);
			if (row_failed_nodes) {
				TS_LOG_ERR("Mutual deviation test failed %d", row_failed_nodes);
				if (stop_test_if_validate_fail)
					strncpy(cts_data->failedreason, "-panel_reason",
						TS_RAWDATA_FAILED_REASON_LEN - 1);
			}
		}
	}
	if (ret == 0) {
		if (dump_test_data_to_user) {
 			memcpy(param->test_data_buf, deviation_row, 2 * mcap_nodes);
			memcpy((param->test_data_buf +  + 2 * mcap_nodes), deviation_col, 2 * mcap_nodes);
			*param->test_data_wr_size = 4 * mcap_nodes;
		}
		ret = row_failed_nodes + col_failed_nodes;
	}
	if (dump_test_data_to_file)
		cts_stop_dump_test_data_to_file();

 free_mem:
	if(deviation_row != NULL)
		kfree(deviation_row);
	if (deviation_col != NULL)
		kfree(deviation_col);
 show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	param->mul_failed_nodes = ret;
	if (ret > 0) {
		TS_LOG_INFO("Deviation test has %d nodes FAIL, ELAPSED TIME: %lldms",
			ret, ktime_to_ms(delta_time));
	} else if (ret < 0) {
		TS_LOG_INFO("Deviation test FAIL %d(%s), ELAPSED TIME: %lldms",
			ret, cts_strerror(ret), ktime_to_ms(delta_time));
	} else {
		TS_LOG_INFO("Deviation test PASS, ELAPSED TIME: %lldms",
			ktime_to_ms(delta_time));
	}

	return ret;
}

int cts_test_sram(struct cts_device *cts_dev, struct cts_test_param *param)
{
	ktime_t start_time, end_time, delta_time;
	int r;
	int ret;
	u8 sram_buf[4];

	if (cts_dev == NULL || param == NULL) {
		TS_LOG_ERR("Sram test with invalid param: cts_dev: %p test param: %p",
			cts_dev, param);
		return -EINVAL;
	}

	TS_LOG_INFO("Sram test, flags: 0x%08x, "
		"drive log file: '%s' buf size: %d",
		param->flags,
		param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	ret = cts_stop_device(cts_dev);
	if (ret) {
		TS_LOG_ERR("Stop device failed %d", ret);
		goto show_test_result;
	}

	cts_lock_device(cts_dev);

	ret = cts_enter_program_mode(cts_dev);
	if (ret) {
		TS_LOG_ERR("Enter program mode failed %d", ret);
		goto unlock_device;
	}

	ret = cts_hw_reg_readsb(cts_dev, CTS_DEV_HW_REG_SRAM_STATUS, sram_buf, 4);
	if (ret) {
		TS_LOG_ERR("READ CTS_DEV_HW_REG_SRAM_STATUS failed %d", ret);
		goto unlock_device;
	}

	TS_LOG_INFO("CTS_DEV_HW_REG_SRAM_STATUS  %d,%d,%d,%d",
		sram_buf[0],sram_buf[1],sram_buf[2],sram_buf[3]);

	if ((sram_buf[0] != 0x02) || (sram_buf[1] != 0x00) ||
		(sram_buf[2] != 0x00) || (sram_buf[3] != 0x00)) {
		TS_LOG_ERR("CTS_DEV_HW_REG_SRAM_STATUS failed %d,%d,%d,%d",
			sram_buf[0],sram_buf[1],sram_buf[2],sram_buf[3]);
		ret = -EINVAL;
		goto unlock_device;
	}

	ret = cts_hw_reg_writeb(cts_dev, CTS_DEV_HW_REG_SRAM_STATUS, 0x03);
	if (ret) {
		TS_LOG_ERR("READ CTS_DEV_HW_REG_SRAM_STATUS failed %d", ret);
		goto unlock_device;
	}

	mdelay(20);

	ret = cts_hw_reg_readsb(cts_dev, CTS_DEV_HW_REG_SRAM_STATUS, sram_buf, 4);
	if (ret) {
		TS_LOG_ERR("READ CTS_DEV_HW_REG_SRAM_STATUS failed %d", ret);
		goto unlock_device;
	}

	TS_LOG_INFO("CTS_DEV_HW_REG_SRAM_STATUS  %d,%d,%d,%d",
		sram_buf[0],sram_buf[1],sram_buf[2],sram_buf[3]);

	if ((sram_buf[0] != 0x07) || (sram_buf[1] != 0x00) ||
		(sram_buf[2] != 0x00) || (sram_buf[3] != 0x00)) {
		TS_LOG_ERR("CTS_DEV_HW_REG_SRAM_STATUS failed %d,%d,%d,%d",
			sram_buf[0],sram_buf[1],sram_buf[2],sram_buf[3]);
		ret =  -ENOMEM;
		goto unlock_device;
	}

 unlock_device:
 	post_test(cts_dev);
	cts_set_normal_addr(cts_dev);
	cts_unlock_device(cts_dev);
	r = cts_start_device(cts_dev);
	if (r)
		TS_LOG_ERR("Start device failed %d", r);

show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	if (ret)
		TS_LOG_INFO("SRAM test FAIL %d(%s), ELAPSED TIME: %lldms",
			 ret, cts_strerror(ret), ktime_to_ms(delta_time));
	else
		TS_LOG_INFO("SRAM test PASS, ELAPSED TIME: %lldms",
			 ktime_to_ms(delta_time));

	return ret;
}

int cts_test_rawdata(struct cts_device *cts_dev, struct cts_test_param *param)
{
	bool driver_validate_data = false;
	bool validate_data_per_node = false;
	bool stop_test_if_validate_fail = false;
	bool dump_test_data_to_user = false;
	bool dump_test_data_to_console = false;
	bool dump_test_data_to_file = false;
	int num_nodes;
	int mcap_nodes, scap_nodes;
	int tsdata_frame_size;
	int frame;
	u16 *rawdata = NULL;
	u16 *mcap_rawdata = NULL;
	u16 *scap_rawdata = NULL;
	ktime_t start_time, end_time, delta_time;
	int i;
	int ret = 0;
	int r;
	struct cts_rawdata_test_priv_param *priv_param;
	struct chipone_ts_data *cts_data =
		container_of(cts_dev, struct chipone_ts_data, cts_dev);

	priv_param = param->priv_param;

	if ((cts_dev == NULL) || (param == NULL) ||
		(param->priv_param_size != sizeof(*priv_param)) ||
		(param->priv_param == NULL) || !(cts_dev->fwdata.rows) ||
		(!cts_dev->fwdata.cols)) {
		TS_LOG_ERR
		    ("Rawdata test with invalid param: priv param: %p size: %d",
		     param->priv_param, param->priv_param_size);
		return -EINVAL;
	}

	if (priv_param->frames <= 0) {
		TS_LOG_INFO("Rawdata test with too little frame %u",
			priv_param->frames);
		return -EINVAL;
	}
	mcap_nodes = cts_dev->fwdata.rows * cts_dev->fwdata.cols;
	scap_nodes = cts_dev->fwdata.rows + cts_dev->fwdata.cols;
	num_nodes = mcap_nodes + scap_nodes;
	tsdata_frame_size = 2 * num_nodes;
	param->mul_failed_nodes = mcap_nodes;
	param->self_failed_nodes = scap_nodes;
	driver_validate_data = !!(param->flags & CTS_TEST_FLAG_VALIDATE_DATA);
	validate_data_per_node =
	    !!(param->flags & CTS_TEST_FLAG_VALIDATE_PER_NODE);
	dump_test_data_to_user =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_USERSPACE);
	dump_test_data_to_console =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_CONSOLE);
	dump_test_data_to_file =
	    !!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE);
	stop_test_if_validate_fail =
	    !!(param->flags & CTS_TEST_FLAG_STOP_TEST_IF_VALIDATE_FAILED);

	TS_LOG_INFO("Rawdata test, flags: 0x%08x, frames: %d, "
		    "num invalid node: %u, "
		    "test data file: '%s' buf size: %d, "
		    "drive log file: '%s' buf size: %d",
		    param->flags, priv_param->frames, param->num_invalid_node,
		    param->test_data_filepath, param->test_data_buf_size,
		    param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	if (dump_test_data_to_user) {
		rawdata = (u16 *)param->test_data_buf;
	} else {
		rawdata = (u16 *)kmalloc(tsdata_frame_size, GFP_KERNEL);
		if (rawdata == NULL) {
			TS_LOG_ERR("Allocate memory for rawdata failed");
			ret = -ENOMEM;
			strncpy(cts_data->failedreason, "-software_reason",
				TS_RAWDATA_FAILED_REASON_LEN - 1);
			goto show_test_result;
		}
	}

	mcap_rawdata = rawdata;
	scap_rawdata = rawdata + mcap_nodes; /*per note 2bytes */

	/* Stop device to avoid un-wanted interrrupt */
	ret = cts_stop_device(cts_dev);
	if (ret) {
		TS_LOG_ERR("Stop device failed %d", ret);
		goto free_mem;
	}

	cts_lock_device(cts_dev);
	ret = prepare_test(cts_dev);
	if (ret) {
		TS_LOG_ERR("Prepare test failed %d", ret);
		goto post_test;
	}

	for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
		r = cts_enable_get_rawdata(cts_dev);
		if (r) {
			TS_LOG_ERR("Enable get tsdata failed %d", r);
			continue;
		} else {
			break;
		}
		mdelay(1);
	}

	if (i >= CFG_CTS_GET_DATA_RETRY) {
		TS_LOG_ERR("Enable read tsdata failed");
		ret = -EIO;
		goto post_test;
	}

	if (dump_test_data_to_file) {
		r = cts_start_dump_test_data_to_file(param->test_data_filepath,
						     !!(param->flags &
							 CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE_APPEND));
		if (r)
			TS_LOG_ERR("Start dump test data to file failed %d", r);
	}

	for (frame = 0; frame < priv_param->frames; frame++) {
		bool data_valid = false;

		for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
			r = cts_get_rawdata(cts_dev, rawdata);
			if (r) {
				TS_LOG_ERR("Get rawdata failed %d", r);
				mdelay(30);
			} else {
				data_valid = true;
				break;
			}
		}

		if (!data_valid) {
			ret = -EIO;
			strncpy(cts_data->failedreason, "-software_reason",
				TS_RAWDATA_FAILED_REASON_LEN - 1);
			break;
		}

		if (dump_test_data_to_user)
			*param->test_data_wr_size += tsdata_frame_size;

		if (dump_test_data_to_console || dump_test_data_to_file) {
			cts_dump_tsdata(cts_dev, "MCap Rawdata", rawdata,
					dump_test_data_to_console);
			cts_dump_tsdata(cts_dev, "SCap Rawdata", scap_rawdata,
					dump_test_data_to_console);
		}

		if (driver_validate_data) {
			param->mul_failed_nodes = validate_tsdata(cts_dev,
				"MCap Rawdata", rawdata, param->invalid_nodes,
				param->num_invalid_node, validate_data_per_node,
				param->min, param->max);
			if (param->mul_failed_nodes) {
				TS_LOG_ERR("MCap Rawdata test failed:%d",
					param->mul_failed_nodes);
				if (stop_test_if_validate_fail)
					strncpy(cts_data->failedreason, "-panel_reason",
						TS_RAWDATA_FAILED_REASON_LEN - 1);
			}
			param->self_failed_nodes = validate_tsdata(cts_dev,
				"SCap Rawdata", scap_rawdata, param->invalid_nodes, 0,
				validate_data_per_node, param->self_min, param->self_max);
			if (param->self_failed_nodes) {
				TS_LOG_ERR("SCap Rawdata test failed:%d",
					param->self_failed_nodes);
				if (stop_test_if_validate_fail) {
					strncpy(cts_data->failedreason, "-panel_reason",
						TS_RAWDATA_FAILED_REASON_LEN - 1);
					break;
				}
			}
		}
	}
	ret = param->mul_failed_nodes + param->self_failed_nodes;
	if (dump_test_data_to_user)
		rawdata += num_nodes;

	if (dump_test_data_to_file)
		cts_stop_dump_test_data_to_file();

	for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
		r = cts_disable_get_rawdata(cts_dev);
		if (r) {
			TS_LOG_ERR("Disable get rawdata failed %d", r);
			continue;
		} else {
			break;
		}
	}

post_test:
	post_test(cts_dev);
	cts_unlock_device(cts_dev);
	r = cts_start_device(cts_dev);
	if (r)
		TS_LOG_ERR("Start device failed %d", r);

free_mem:
	if (!dump_test_data_to_user && rawdata != NULL)
		kfree(rawdata);

show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	if (ret > 0) {
		TS_LOG_INFO
		    ("Rawdata test has %d nodes FAIL, ELAPSED TIME: %lldms",
		     ret, ktime_to_ms(delta_time));
	} else if (ret < 0) {
		TS_LOG_INFO("Rawdata test FAIL %d(%s), ELAPSED TIME: %lldms",
			    ret, cts_strerror(ret), ktime_to_ms(delta_time));
	} else {
		TS_LOG_INFO("Rawdata test PASS, ELAPSED TIME: %lldms",
			    ktime_to_ms(delta_time));
	}

	return ret;
}

int cts_test_noise(struct cts_device *cts_dev, struct cts_test_param *param)
{
	struct cts_noise_test_priv_param *priv_param;
	bool driver_validate_data = false;
	bool validate_data_per_node = false;
	bool dump_test_data_to_user = false;
	bool dump_test_data_to_console = false;
	bool dump_test_data_to_file = false;
	int num_nodes;
	int mcap_nodes, scap_nodes;
	int tsdata_frame_size;
	int frame;
	u16 *buffer = NULL;
	int buf_size = 0;
	u16 *curr_rawdata = NULL;
	u16 *max_rawdata = NULL;
	u16 *min_rawdata = NULL;
	u16 *noise = NULL;
	bool first_frame = true;
	ktime_t start_time, end_time, delta_time;
	int i;
	int ret;
	int fail_nodes = 0;
	int freq_hop_num = 0;
	int r = 0;
	u32 total_frames;

	priv_param = param->priv_param;
	if ((cts_dev == NULL) || (param == NULL) ||
		(param->priv_param_size != sizeof(*priv_param)) ||
		(param->priv_param == NULL) || !(cts_dev->fwdata.rows) ||
		(!cts_dev->fwdata.cols)) {
		TS_LOG_ERR
			("Noise test with invalid param: priv param: %p size: %d",
			param->priv_param, param->priv_param_size);
		return -EINVAL;
	}

	if (priv_param->frames < 2) {
		TS_LOG_ERR("Noise test with too little frame %u",
			priv_param->frames);
		return -EINVAL;
	}
	mcap_nodes = cts_dev->fwdata.rows * cts_dev->fwdata.cols;
	scap_nodes = cts_dev->fwdata.rows + cts_dev->fwdata.cols;
	num_nodes = mcap_nodes + scap_nodes;
	tsdata_frame_size = 2 * num_nodes;
	param->mul_failed_nodes	= mcap_nodes;
	param->self_failed_nodes = scap_nodes;
	driver_validate_data = !!(param->flags & CTS_TEST_FLAG_VALIDATE_DATA);
	validate_data_per_node =
		!!(param->flags & CTS_TEST_FLAG_VALIDATE_PER_NODE);
	dump_test_data_to_user =
		!!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_USERSPACE);
	dump_test_data_to_console =
		!!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_CONSOLE);
	dump_test_data_to_file =
		!!(param->flags & CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE);

	TS_LOG_INFO("Noise test, flags: 0x%08x, frames: %d, "
		"num invalid node: %u, "
		"test data file: '%s' buf size: %d, "
		"drive log file: '%s' buf size: %d",
		param->flags, priv_param->frames, param->num_invalid_node,
		param->test_data_filepath, param->test_data_buf_size,
		param->driver_log_filepath, param->driver_log_buf_size);

	start_time = ktime_get();

	buf_size = (driver_validate_data ? 4 : 1) * tsdata_frame_size;
	buffer = (u16 *)kmalloc(buf_size, GFP_KERNEL);
	if (buffer == NULL) {
		TS_LOG_ERR("Alloc mem for touch data failed");
		ret = -ENOMEM;
		goto show_test_result;
	}

	curr_rawdata = buffer;
	if (driver_validate_data) {
		TS_LOG_INFO("driver_validate_data:%d", driver_validate_data);
		max_rawdata = curr_rawdata + 1 * num_nodes;
		min_rawdata = curr_rawdata + 2 * num_nodes;
		noise = curr_rawdata + 3 * num_nodes;
	}

	/* Stop device to avoid un-wanted interrrupt */
	ret = cts_stop_device(cts_dev);
	if (ret) {
		TS_LOG_ERR("Stop device failed %d", ret);
		goto free_mem;
	}

	cts_lock_device(cts_dev);
	ret = prepare_test(cts_dev);
	if (ret) {
		TS_LOG_ERR("Prepare test failed %d", ret);
		goto post_test;
	}


	if (dump_test_data_to_file) {
		r = cts_start_dump_test_data_to_file(param->test_data_filepath,
			!!(param->flags &
			CTS_TEST_FLAG_DUMP_TEST_DATA_TO_FILE_APPEND));
		if (r)
			TS_LOG_ERR("Start dump test data to file failed %d", r);
	}

	do {
		for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
			r = cts_enable_get_rawdata(cts_dev);
			if (r) {
				TS_LOG_ERR("Enable get tsdata failed %d", r);
				continue;
			} else {
				break;
			}
		}

		if (i >= CFG_CTS_GET_DATA_RETRY) {
			TS_LOG_ERR("Enable read tsdata failed");
			ret = -EIO;
			goto stop_dump_tsdata;
		}

		first_frame = true;
		priv_param->frames_ignore = 0;
		TS_LOG_INFO("frames_ignore:%d\n", priv_param->frames_ignore);
		total_frames = priv_param->frames_ignore + priv_param->frames;
		for (frame = 0; frame < total_frames; frame++) {
			for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
				r = cts_get_rawdata(cts_dev, curr_rawdata);
				if (r) {
					TS_LOG_ERR("Get rawdata failed %d", r);
					mdelay(30);
				} else {
					break;
				}
			}

			if (i >= CFG_CTS_GET_DATA_RETRY) {
				TS_LOG_ERR("Read rawdata failed");
				ret = -EIO;
				goto stop_dump_tsdata;
			}

			if (frame < priv_param->frames_ignore)
				continue;

			if (dump_test_data_to_console || dump_test_data_to_file) {
  				cts_dump_tsdata(cts_dev, "Noise Test - MCap Rawdata",
 					curr_rawdata, (!dump_test_data_to_console));
				cts_dump_tsdata(cts_dev, "Noise Test - SCap Rawdata",
					&curr_rawdata[mcap_nodes], (!dump_test_data_to_console));
			}
			if (driver_validate_data) {
				if (unlikely(first_frame)) {
					memcpy(max_rawdata, curr_rawdata,
						tsdata_frame_size);
					memcpy(min_rawdata, curr_rawdata,
						tsdata_frame_size);
				first_frame = false;
				} else {
					for (i = 0; i < num_nodes; i++) {
						if (curr_rawdata[i] > max_rawdata[i]) {
							max_rawdata[i] = curr_rawdata[i];
						} else if (curr_rawdata[i] < min_rawdata[i]) {
							min_rawdata[i] = curr_rawdata[i];
						}
					}
				}
			}
		}

		for (i = 0; i < num_nodes; i++)
			noise[i] = max_rawdata[i] - min_rawdata[i];

		if (dump_test_data_to_user) {
			memcpy(param->test_data_buf + (freq_hop_num + 0) * tsdata_frame_size,
				noise, tsdata_frame_size);
			*param->test_data_wr_size += tsdata_frame_size;
		}
		if (driver_validate_data) {
			if (dump_test_data_to_console || dump_test_data_to_file) {
				cts_dump_tsdata(cts_dev, "MCap MAX", max_rawdata,
					dump_test_data_to_console);
				cts_dump_tsdata(cts_dev, "MCap MIN", min_rawdata,
					dump_test_data_to_console);
				cts_dump_tsdata(cts_dev, "MCap Noise", noise,
					dump_test_data_to_console);
				cts_dump_tsdata(cts_dev, "SCap MAX", &max_rawdata[mcap_nodes],
					dump_test_data_to_console);
				cts_dump_tsdata(cts_dev, "SCap MIN", &min_rawdata[mcap_nodes],
					dump_test_data_to_console);
				cts_dump_tsdata(cts_dev, "SCap Noise", &noise[mcap_nodes],
					dump_test_data_to_console);
			}

			TS_LOG_DEBUG("Noise threthod:%d,%d",
				param->max_limits[0],param->max_limits[mcap_nodes+0]);
			param->mul_failed_nodes = validate_tsdata(cts_dev,
				"MCap Noise test", noise,
				param->invalid_nodes, param->num_invalid_node,
				validate_data_per_node, NULL, param->max);
			if (param->mul_failed_nodes)
				TS_LOG_ERR("MCap Noise test failed:%d",
					param->mul_failed_nodes);

			param->self_failed_nodes = validate_tsdata(cts_dev,
				"SCap Noise test", &noise[mcap_nodes],
				param->invalid_nodes, 0,
				validate_data_per_node, NULL, &param->max[mcap_nodes]);
			if (param->self_failed_nodes)
				TS_LOG_ERR("SCap noise test failed:%d",
					param->self_failed_nodes);

			fail_nodes += param->mul_failed_nodes + param->self_failed_nodes;
		}

		for (i = 0; i < CFG_CTS_GET_DATA_RETRY; i++) {
			r = cts_disable_get_rawdata(cts_dev);
			if (r) {
				TS_LOG_ERR("Disable get rawdata failed %d", r);
				continue;
			} else {
				break;
			}
		}
		freq_hop_num++;
	} while(freq_hop_num < FREQ_HOP_NUM_MAX);

stop_dump_tsdata:
	if (dump_test_data_to_file)
		cts_stop_dump_test_data_to_file();

post_test:
	post_test(cts_dev);

	cts_unlock_device(cts_dev);
	r = cts_start_device(cts_dev);
	if (r)
		TS_LOG_ERR("Start device failed %d", r);

free_mem:
	if (buffer != NULL) {
		kfree(buffer);
	}

show_test_result:
	end_time = ktime_get();
	delta_time = ktime_sub(end_time, start_time);
	if (fail_nodes) {
		TS_LOG_INFO("Noise test has %d nodes FAIL, ELAPSED TIME: %lldms",
			 fail_nodes, ktime_to_ms(delta_time));
			 return fail_nodes;
	}

	if (ret != 0) {
		TS_LOG_INFO("Noise test FAIL %d(%s), ELAPSED TIME: %lldms",
			 ret, cts_strerror(ret), ktime_to_ms(delta_time));
	} else {
		TS_LOG_INFO("Noise test PASS, ELAPSED TIME: %lldms",
			 ktime_to_ms(delta_time));
	}

	return ret;
}
