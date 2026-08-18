/**
 * Copyright (c) 2017-2018 e-con Systems India Pvt. Ltd.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __CAM_COMMON_H__
#define __CAM_COMMON_H__

#include <linux/ioctl.h>  /* For IOCTL macros */
#include <media/nvc.h>
//#include <media/nvc_image.h>
#include <media/camera_common.h>
#include <linux/miscdevice.h>
#include <media/tegra-v4l2-camera.h>

#define FUSE_ID_SIZE		6
#define AR2020_VERSION		"1.1.0-ge3f455"

/* Defines related to MCU */

#define CMD_SIGNATURE			0x43
#define TX_LEN_PKT			5
#define RX_LEN_PKT			6
#define HEADER_FOOTER_SIZE		4
#define CMD_STATUS_MSG_LEN		7
#define MAX_CTRL_DATA_LEN 		100
#define MAX_CTRL_UI_STRING_LEN 		32
#define MAX_CTRL_MENU_ELEM 		20
#define MAX_NUM_FRATES			10
#define MAX_NUM_FMTS			20
#define NUM_CTRLS			10
#define VERSION_SIZE			32
#define MAX_ATTEMPTS			10
#define CAM_MASTER_MODE			0x00
#define CAM_SLAVE_MODE			0x01
#define EXPOSURE_30HZ			33333
#define EXPOSURE_60HZ			16667

#define MCU_CMD_STATUS_SUCCESS		0x0000
#define MCU_CMD_STATUS_PENDING		0xF000
#define MCU_CMD_STATUS_ISP_PWDN		0x0FF0
#define MCU_CMD_STATUS_ISP_UNINIT	0x0FF1
#define EXTENDED_CTRL_LENGTH		32
#define EXTENDED_CTRL_SIZE		8

#define V4L2_CID_GAIN_ORIG		0x00980913
#define IMX415_DEFAULT_DATAFMT	MEDIA_BUS_FMT_SGBRG12_1X12
#define IMX415_HDR_DATAFMT	MEDIA_BUS_FMT_SGBRG10_1X10
#define DEBUG_CONTROLS_ENABLE 		1

// Added to load mcu firmware bin from package
#define VERSION_FILE_OFFSET			99


static const u32 def_ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_EXPOSURE_SHORT,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_FUSE_ID,
	TEGRA_CAMERA_CID_HDR_EN,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

typedef enum _errno
{
        ERRCODE_SUCCESS = 0x00,
        ERRCODE_BUSY = 0x01,
        ERRCODE_INVAL = 0x02,
        ERRCODE_PERM = 0x03,
        ERRCODE_NODEV = 0x04,
        ERRCODE_IO = 0x05,
        ERRCODE_HW_SPEC = 0x06,
        ERRCODE_AGAIN = 0x07,
        ERRCODE_ALREADY = 0x08,
        ERRCODE_NOTIMPL = 0x09,
        ERRCODE_RANGE = 0x0A,

        /*   Reserved 0x0B - 0xFE */

        ERRCODE_UNKNOWN = 0xFF,
} RETCODE;

typedef enum _cmd_id
{
        CMD_ID_VERSION = 0x00,
        CMD_ID_GET_SENSOR_ID = 0x01,
        CMD_ID_GET_STREAM_INFO = 0x02,
        CMD_ID_GET_CTRL_INFO = 0x03,
        CMD_ID_INIT_CAM = 0x04,
        CMD_ID_GET_STATUS = 0x05,
        CMD_ID_DE_INIT_CAM = 0x06,
        CMD_ID_STREAM_ON = 0x07,
        CMD_ID_STREAM_OFF = 0x08,
        CMD_ID_STREAM_CONFIG = 0x09,
	CMD_ID_GET_CTRL_UI_INFO = 0x0A,

        /* Reserved 0x0B to 0x0F */

        CMD_ID_GET_CTRL = 0x10,
        CMD_ID_SET_CTRL = 0x11,
        CMD_ID_ISP_READ = 0x12,
        CMD_ID_ISP_WRITE = 0x13,
        CMD_ID_FW_UPDT = 0x14,
        CMD_ID_ISP_PDOWN = 0x15,
        CMD_ID_ISP_PUP = 0x16,

	/* Configuring MIPI Lanes */
	CMD_ID_LANE_CONFIG = 0x17,
	CMD_ID_MIPI_CLK_CONFIG = 0x18,
        /* Reserved - 0x19 to 0xFE (except 0x43) */

        CMD_ID_UNKNOWN = 0xFF,

} HOST_CMD_ID;

enum
{
        FRAME_RATE_DISCRETE = 0x01,
        FRAME_RATE_CONTINOUS = 0x02,
};

enum
{
        CTRL_STANDARD = 0x01,
        CTRL_EXTENDED = 0x02,
};

enum
{
/*  0x01 - Integer (32bit)
		0x02 - Long Int (64 bit)
		0x03 - String
		0x04 - Pointer to a 1-Byte Array
		0x05 - Pointer to a 2-Byte Array
		0x06 - Pointer to a 4-Byte Array
		0x07 - Pointer to Generic Data (custom Array)
*/

        EXT_CTRL_TYPE_INTEGER = 0x01,
        EXT_CTRL_TYPE_LONG = 0x02,
        EXT_CTRL_TYPE_STRING = 0x03,
        EXT_CTRL_TYPE_PTR8 = 0x04,
        EXT_CTRL_TYPE_PTR16 = 0x05,
        EXT_CTRL_TYPE_PTR32 = 0x06,
        EXT_CTRL_TYPE_VOID = 0x07,
};


typedef struct _isp_stream_info
{
        uint32_t fmt_fourcc;
        uint16_t width;
        uint16_t height;
        uint8_t frame_rate_type;
        union
        {
                struct
                {
                        uint16_t frame_rate_num;
                        uint16_t frame_rate_denom;
                } disc;
                struct
                {
                        uint16_t frame_rate_min_num;
                        uint16_t frame_rate_min_denom;
                        uint16_t frame_rate_max_num;
                        uint16_t frame_rate_max_denom;
                        uint16_t frame_rate_step_num;
                        uint16_t frame_rate_step_denom;
                } cont;
        } frame_rate;
} ISP_STREAM_INFO;


typedef struct _isp_ctrl_ui_info {
	struct {
		char ctrl_name[MAX_CTRL_UI_STRING_LEN];
		uint8_t ctrl_ui_type;
		uint8_t ctrl_ui_flags;
	}ctrl_ui_info;

	/* This Struct is valid only if ctrl_ui_type = 0x03 */
	struct {
		uint8_t num_menu_elem;
		char **menu;
		long *menu_int;
	}ctrl_menu_info;
} ISP_CTRL_UI_INFO;



typedef struct _isp_ctrl_info_std
{
        uint32_t ctrl_id;
        uint8_t ctrl_type;
        union
        {
                struct
                {
                        int32_t ctrl_min;
                        int32_t ctrl_max;
                        int32_t ctrl_def;
                        int32_t ctrl_step;
                } std;
                struct
                {
                        uint8_t val_type;
                        uint32_t val_length;
                        // This size may vary according to ctrl types
						uint64_t ctrl_min;
						uint64_t ctrl_max;
						uint64_t ctrl_def;
						uint64_t ctrl_step;
						uint8_t val_data[MAX_CTRL_DATA_LEN];
				} ext;
        } ctrl_data;
	ISP_CTRL_UI_INFO ctrl_ui_data;

} ISP_CTRL_INFO;



struct cam_mode {
	__u32 xres;
	__u32 yres;
	__u32 frame_length;
	__u32 coarse_time;
	__u32 coarse_time_short;
	__u16 gain;
	__u8 hdr_en;
};

struct cam_hdr {
	__u32 coarse_time_long;
	__u32 coarse_time_short;
};

struct cam_ae {
	__u32 frame_length;
	__u8  frame_length_enable;
	__u32 coarse_time;
	__u32 coarse_time_short;
	__u8  coarse_time_enable;
	__s32 gain;
	__u8  gain_enable;
};

struct cam_sensordata {
	__u32 fuse_id_size;
	__u8  fuse_id[FUSE_ID_SIZE];
};

#ifdef __KERNEL__
struct cam_power_rail {
	struct regulator *dvdd;
	struct regulator *avdd;
	struct regulator *iovdd;
	struct regulator *ext_reg1;
	struct regulator *ext_reg2;
	struct clk *mclk;
	unsigned int pwdn_gpio;
	unsigned int cam1_gpio;
	unsigned int reset_gpio;
	unsigned int af_gpio;
};

struct cam {
	struct camera_common_power_rail	power;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct i2c_client		*i2c_client;
	struct v4l2_subdev		*subdev;
	const struct econ_cam_sensor_info  *sensor_info;
	u8 fude_id[6];
	u32				frame_length;
	s64 last_wdr_et_val;
	ISP_STREAM_INFO			*stream_info;
	ISP_CTRL_INFO 			*cam_ctrl_info;
	uint32_t 			*ctrldb;
	struct camera_common_data	*s_data;
	struct camera_common_frmfmt 	*cam_frmfmt;
	int 				num_ctrls;
	int 				*streamdb;
	struct tegracam_device		*tc_dev;
	int				frmfmt_mode;
	int 				format_fourcc;
	int 				frate_index;
	uint8_t				mipi_lane_config;
	uint16_t			mipi_clock_config;
	bool				use_dol_wdr_mode;
	unsigned int			reset_gpio;
	unsigned int			boot_gpio;
	unsigned int			flash_gpio;
	bool				use_sensor_mode_id;
	bool				mipi_clk_configurable;
	struct mutex 			cam_i2c_mutex;
};

bool g_initialized = false;

// Added to load MCU firmware bin along with package
const char *mcu_fw_name = NULL;
const struct firmware *mcu_fw;
static uint8_t is_fw_loaded = 0;
unsigned char *mcu_fw_buf = NULL;

struct cam_platform_data {
	const char *mclk_name; /* NULL for default default_mclk */
	unsigned int cam1_gpio;
	unsigned int reset_gpio;
	unsigned int af_gpio;
	bool ext_reg;
	int (*power_on)(struct cam_power_rail *pw);
	int (*power_off)(struct cam_power_rail *pw);
};
#endif /* __KERNEL__ */

static int cam_set_mode(struct tegracam_device *tc_dev);
static int cam_read(struct i2c_client *client, u8 * val, u32 count);
static int cam_write(struct i2c_client *client, u8 * val, u32 count);
static int cam_list_fmts(struct i2c_client *client, struct cam *priv,
			  ISP_STREAM_INFO *stream_info,int *frm_fmt_size);
static int cam_list_ctrls(struct i2c_client *client, struct cam *priv,
                          ISP_CTRL_INFO * cam_ctrl_info);


unsigned char errorcheck(char *data, unsigned int len);
static int is_fw_update_required(struct i2c_client *client, struct cam *priv,
		unsigned char * fw_version, unsigned char *txt_fw_version);
static int cam_get_cmd_status(struct i2c_client *client, uint8_t * cmd_id,
                              uint16_t * cmd_status, uint8_t * ret_code);
static int cam_init(struct i2c_client *client);
static int cam_stream_config(struct i2c_client *client, struct cam *priv, uint32_t format,
                             int mode, int frate_index);
static int cam_set_ctrl(struct i2c_client *client,struct cam *priv,  uint32_t ctrl_id,
                        uint8_t ctrl_type, int64_t curr_val);
//static int cam_get_ctrl_ui(struct i2c_client *client,
//                          ISP_CTRL_INFO * cam_ui_info, int index);
static int cam_fw_update(struct i2c_client *client, unsigned char *cam_fw_version);

static int cam_stream_on(struct i2c_client *client, struct cam *priv);
static int cam_stream_off(struct i2c_client *client, struct cam *priv);
static int cam_set_exposure(struct tegracam_device *tc_dev, s64 val);

#endif  /* __CAM_COMMON_H__ */
