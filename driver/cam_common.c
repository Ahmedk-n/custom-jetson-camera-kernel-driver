/*
 * can_common.c - Nvidia ISP based e-con Camera sensor driver
 *
 * Copyright (c) 2017-2018, e-con Systems India Pvt. Ltd.  All rights reserved.
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

#define DEBUG

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>

#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/firmware.h>

#include <media/tegracam_core.h>
#include "cam_firmware.h"

#define DEBUG_PRINTK
#ifndef DEBUG_PRINTK
	#define debug_printk(s , ... )
#else
	#define debug_printk(s, ... ) printk( s, ##__VA_ARGS__)
#endif


#include "cam_common.h"


struct econ_cam_sensor_info {
	u32 numctrls;
	const u32 *ctrl_cid_list;
	enum v4l2_ctrl_type ctrl_type;
	bool flash_gpios_used;
	bool master_slct_used;
	u64 exp_scaling_factor;
};

enum {
	eimx290,
	ar0521,
	ar2020,
};


static const u32 imx290_ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_EXPOSURE_SHORT,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_FUSE_ID,
	TEGRA_CAMERA_CID_HDR_EN,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

static const u32 ar0521_ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

static const u32 ar2020_ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_EXPOSURE_SHORT,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_FUSE_ID,
	TEGRA_CAMERA_CID_HDR_EN,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

static const struct econ_cam_sensor_info econ_cam_sensor_info_tbl[] = {
	[eimx290] = {
		.numctrls		= ARRAY_SIZE(imx290_ctrl_cid_list),
		.ctrl_cid_list		= imx290_ctrl_cid_list,
		.flash_gpios_used	= true,
		.master_slct_used	= true,
		.ctrl_type		= CTRL_EXTENDED,
		.exp_scaling_factor	= FIXED_POINT_SCALING_FACTOR,
	},
	[ar0521] = {
		.numctrls		= ARRAY_SIZE(ar0521_ctrl_cid_list),
		.ctrl_cid_list		= ar0521_ctrl_cid_list,
		.flash_gpios_used	= false,
		.master_slct_used	= false,
		.ctrl_type		= CTRL_STANDARD,
		.exp_scaling_factor	= 10000,
	},
	[ar2020] = {
		.numctrls		= ARRAY_SIZE(ar2020_ctrl_cid_list),
		.ctrl_cid_list		= ar2020_ctrl_cid_list,
		.flash_gpios_used	= false,
		.master_slct_used	= false,
		.ctrl_type		= CTRL_EXTENDED,
		.exp_scaling_factor	= FIXED_POINT_SCALING_FACTOR,
	},
};

static const struct regmap_config cam_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
};

static int cam_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;

	if (pdata && pdata->power_on) {
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev,"%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	if (unlikely(!(pw->avdd || pw->iovdd || pw->dvdd)))
		goto skip_power_seqn;
#if 0
	if (pw->dvdd)
		err = regulator_enable(pw->dvdd);
	if (err)
		goto cam_dvdd_fail;
 
	if (pw->iovdd)
		err = regulator_enable(pw->iovdd);
	if (err)
		goto cam_iovdd_fail;

	if (pw->avdd)
		err = regulator_enable(pw->avdd);
	if (err)
		goto cam_avdd_fail;

	pw->state = SWITCH_ON;
#endif
	return 0;

skip_power_seqn:
	if (pw->af_gpio)
		gpio_set_value(pw->af_gpio, 0);

	pw->state = SWITCH_ON;

	return 0;
#if 0
cam_avdd_fail:
	regulator_disable(pw->iovdd);

cam_iovdd_fail:
	regulator_disable(pw->dvdd);

cam_dvdd_fail:
	dev_err(dev,"%s failed.\n", __func__);

	return err;
#endif
}

static int cam_power_off(struct camera_common_data *s_data)
{
	struct camera_common_power_rail *pw = s_data->power;

	pw->state = SWITCH_OFF;

	return 0;
}

static int cam_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_power_rail *pw = tc_dev->s_data->power;
	if (unlikely(!pw))
		return -EFAULT;

	if (likely(pw->dvdd))
		devm_regulator_put(pw->dvdd);

	if (likely(pw->avdd))
		devm_regulator_put(pw->avdd);

	if (likely(pw->iovdd))
		devm_regulator_put(pw->iovdd);

	pw->avdd = NULL;
	pw->iovdd = NULL;
	pw->dvdd = NULL;

	return 0;
}

static inline int cam_read_reg(struct camera_common_data *s_data,
				u16 addr, u8 *val)
{
    	int err = 0;
	dev_dbg(s_data->dev, "%s: read reg\n", __func__);
	return err;
}

static int cam_write_reg(struct camera_common_data *s_data,
				u16 addr, u8 val)
{
    	int err = 0;
	struct device *dev = s_data->dev;

	dev_dbg(dev, "%s: write reg\n", __func__);

	return err;
}


static int cam_power_get(struct tegracam_device *tc_dev)
{
	int err = 0;
        struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct clk *parent;

	if(pdata->mclk_name) {
		pw->mclk = devm_clk_get(dev, pdata->mclk_name);
		if (IS_ERR(pw->mclk)) {
			dev_err(dev,"unable to get clock %s\n", pdata->mclk_name);
			return PTR_ERR(pw->mclk);
		}

		parent = devm_clk_get(dev, "pllp_grtba");
		if (IS_ERR(parent))
			dev_err(dev,"devm_clock_get failed for pllp_grtba");
		 else
			clk_set_parent(pw->mclk, parent);
	}

	if (pdata->regulators.avdd)
		err |= camera_common_regulator_get(dev,&pw->avdd,
				pdata->regulators.avdd);

	if (pdata->regulators.iovdd)
		err |= camera_common_regulator_get(dev,&pw->iovdd,
				pdata->regulators.iovdd);

	if (!err) {
		pw->reset_gpio = pdata->reset_gpio;
		pw->af_gpio = pdata->af_gpio;
		pw->pwdn_gpio = pdata->pwdn_gpio;
	}

	pw->state = SWITCH_OFF;

	return err;
}

static void gpio_set_output_value(unsigned int gpio, int val)
{
    if (gpiod_cansleep(gpio_to_desc(gpio))) {
	gpio_direction_output(gpio,val);
	gpio_set_value_cansleep(gpio, val);
    } else {
	gpio_direction_output(gpio,val);
	gpio_set_value(gpio, val);
    }
}

static int cam_set_mode(struct tegracam_device *tc_dev)
{
    	struct cam *priv = (struct cam*)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	struct device *dev = tc_dev->dev;
	int err = 0, retry=2;

	s_data->override_enable = false;

	switch(s_data->colorfmt->code) {
		case MEDIA_BUS_FMT_SGBRG12_1X12:
			printk("--------INSIDE MEDIS BUS 12BPP GBRG---------\n");
			priv->format_fourcc = V4L2_PIX_FMT_SGBRG12;
			break;

		case MEDIA_BUS_FMT_SGBRG10_1X10:
			printk("--------INSIDE MEDIS BUS 10BPP GBRG---------\n");
			priv->format_fourcc = V4L2_PIX_FMT_SGBRG10;
			break;

		case MEDIA_BUS_FMT_SRGGB10_1X10:
			printk("--------INSIDE MEDIS BUS 10BPP RGGB---------\n");
			priv->format_fourcc = V4L2_PIX_FMT_SRGGB10;
			break;
		case MEDIA_BUS_FMT_SRGGB12_1X12:
			printk("--------INSIDE MEDIS BUS 12BPP RGGB---------\n");
			priv->format_fourcc = V4L2_PIX_FMT_SRGGB12;
			break;
		case MEDIA_BUS_FMT_SGRBG10_1X10:
			printk("--------INSIDE MEDIS BUS 10BPP GRBG---------\n");
			priv->format_fourcc = V4L2_PIX_FMT_SGRBG10;
			break;

		case MEDIA_BUS_FMT_UYVY8_1X16:
			priv->format_fourcc = V4L2_PIX_FMT_UYVY;
			break;
	default:
		priv->format_fourcc = V4L2_PIX_FMT_SRGGB10;
		/* Not Implemented */
		break;
	}

	/* numfrmfmts is registered as the MCU's total stream count, but only the
	 * Bayer streams fill cam_frmfmt[] - the framework can hand us an index
	 * pointing at an empty (all-zero) entry, and the MCU then correctly
	 * rejects a 0x0@0 stream config with ERRCODE_RANGE. Validate here and
	 * recover by matching the requested resolution. */
	if (s_data->mode_prop_idx < 0 ||
	    priv->cam_frmfmt[s_data->mode_prop_idx].size.width == 0) {
		int fixed = 0, i;
		for (i = 0; priv->cam_frmfmt[i].size.width != 0; i++) {
			if (priv->cam_frmfmt[i].size.width == s_data->fmt_width &&
			    priv->cam_frmfmt[i].size.height == s_data->fmt_height) {
				fixed = i;
				break;
			}
		}
		dev_warn(dev, "set_mode: mode_prop_idx %d has no stream, using mode %d for %dx%d\n",
			 s_data->mode_prop_idx, fixed, s_data->fmt_width, s_data->fmt_height);
		s_data->mode_prop_idx = fixed;
	}

	while (retry-- > 0 ) {
		if((err = cam_set_ctrl(priv->i2c_client, priv, TEGRA_CAMERA_CID_SENSOR_MODE_ID,
					CTRL_STANDARD, (int64_t)s_data->mode_prop_idx)) < 0)
			dev_err(dev,"%s[%d]\n",__func__,__LINE__);
		else
			break;
	}
	retry = 5;
	priv->format_fourcc = s_data->colorfmt->pix_fmt;
	s_data->sensor_mode_id = s_data->mode_prop_idx;
	priv->frmfmt_mode = s_data->mode_prop_idx;

	while(retry > 0) {
		err = cam_stream_config(priv->i2c_client, priv, priv->format_fourcc,
				priv->frmfmt_mode, priv->frate_index);
		if(err != 0) {
			retry--;
			msleep(100);

		} else
			break;
	}

	if (retry == 0) {
		return err;
	}

	return err;
}

static int cam_start_streaming(struct tegracam_device *tc_dev)
{
	struct cam *priv = (struct cam *)tegracam_get_privdata(tc_dev);
	int err = 0, retry = 5;

	while (--retry > 0) {
		err = cam_stream_on(priv->i2c_client, priv);
		if(err!= 0){
			dev_err(tc_dev->dev,"%s (%d) retrying Stream_On \n", __func__, __LINE__);
			continue;
		} else {
			break;
		}
	}
	return err;
}

static int cam_stop_streaming(struct tegracam_device *tc_dev)
{
	struct cam *priv = (struct cam *)tegracam_get_privdata(tc_dev);
	int err = 0, retry = 5;

	while (--retry > 0) {
		err = cam_stream_off(priv->i2c_client, priv);
		if(err!= 0){
			dev_err(tc_dev->dev,"%s (%d) retrying Stream_Off \n", __func__, __LINE__);
			continue;
		} else {
			break;
		}
	}
	return err;
}

#ifdef CONFIG_OF

#define ECON_CAM_COMPATIBLE(of_compatible, cfg) {		\
			.compatible = of_compatible,		\
			.data = &econ_cam_sensor_info_tbl[cfg],	\
}

static struct of_device_id cam_of_match[] = {
	ECON_CAM_COMPATIBLE("nvidia,eimx290", eimx290),
	ECON_CAM_COMPATIBLE("nvidia,ar0521", ar0521),
	ECON_CAM_COMPATIBLE("nvidia,ar2020",ar2020),
	{ },
};

#endif

static int cam_set_gain(struct tegracam_device *tc_dev, s64 val)
{
    	struct cam *priv = (struct cam *)tc_dev->priv;
	struct camera_common_data *s_data = priv->s_data;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode];
	struct device *dev = tc_dev->dev;
	int err = 0, retry = 5;
	uint64_t data = val;

		
	if(mode->control_properties.min_gain_val == mode->control_properties.max_gain_val)
		return 0;

	if(data < mode->control_properties.min_gain_val)
		data = mode->control_properties.min_gain_val;
	while (--retry > 0) {
		if ((err = cam_set_ctrl(tc_dev->client, priv, TEGRA_CAMERA_CID_GAIN, priv->sensor_info->ctrl_type, data)) < 0) {
			dev_err(dev, "%s[%d] Fail! retrying\n",__func__,__LINE__);
			continue;
		} else {
			return 0;
		}
	}
	dev_err(dev, "%s[%d] Failed after retries!\n",__func__,__LINE__);
	return -EINVAL;
}

static int cam_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
    	struct cam *priv = (struct cam *)tc_dev->priv;
	struct device *dev = tc_dev->dev;
	int err = 0, retry = 5;
	uint64_t data = val; //(val * priv->sensor_info->exp_scaling_factor) / mode->control_properties.exposure_factor;

	

		while (--retry > 0) {
			if ((err = cam_set_ctrl(tc_dev->client, priv, TEGRA_CAMERA_CID_EXPOSURE, priv->sensor_info->ctrl_type, data)) < 0) {
				dev_err(dev, "%s[%d] Fail! retrying\n",__func__,__LINE__);
				continue;
			} else {
				return 0;
			}
		}
	
	dev_err(dev, "%s[%d] Failed after retries!\n",__func__,__LINE__);
	return -EINVAL;
}

static int cam_set_exposure_short(struct tegracam_device *tc_dev, s64 val)
{
    	struct cam *priv = (struct cam *)tc_dev->priv;
	struct device *dev = tc_dev->dev;
	int err = 0;
	uint64_t data = val ; //* FIXED_POINT_SCALING_FACTOR / mode->control_properties.exposure_factor;

	if ((err = cam_set_ctrl(tc_dev->client, priv, TEGRA_CAMERA_CID_EXPOSURE_SHORT, CTRL_EXTENDED, data)) < 0) {
	   	dev_err(dev, "%s[%d] Fail!\n",__func__,__LINE__);
		return -EINVAL;
	}
	else
	{
		printk("%s, value: %lld \n\r", __func__, data);
		return 0;
	}
	return 0;
}

static int cam_set_framerate(struct tegracam_device *tc_dev, s64 val)
{
    	struct cam *priv = (struct cam *)tc_dev->priv;
	struct camera_common_data *s_data = priv->s_data;
	const struct sensor_mode_properties *mode =
	    &s_data->sensor_props.sensor_modes[s_data->mode];
	struct device *dev = tc_dev->dev;
	int err = 0, retry = 5;

	uint64_t data = val;
	

		
        if(mode->control_properties.min_framerate == mode->control_properties.max_framerate)
                return 0;

	while (--retry > 0) {
		if ((err = cam_set_ctrl(tc_dev->client, priv, TEGRA_CAMERA_CID_FRAME_RATE, CTRL_EXTENDED, data)) < 0) {
		   	dev_err(dev, "%s[%d] Fail! retrying\n",__func__,__LINE__);
			continue;
		} else {
			return 0;
		}
	}
	dev_err(dev, "%s[%d] Failed after retries!\n",__func__,__LINE__);
	return -EINVAL;
}

static int cam_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	/* FIXME: Grouphold Currently not supported */
	return 0;
}


static int cam_fill_string_ctrl(struct tegracam_device *tc_dev,
				struct v4l2_ctrl *ctrl)
{
	struct device *dev = tc_dev->dev;
	int err = 0;

	dev_dbg(dev, "%s[%d] Set!\n",__func__,__LINE__);

	return err;
}

struct tegracam_ctrl_ops cam_ctrl_ops = {
	.string_ctrl_size	= {0, FUSE_ID_SIZE},
	.set_gain		= cam_set_gain,
	.set_exposure		= cam_set_exposure,
	.set_exposure_short	= cam_set_exposure_short,
	.set_frame_rate		= cam_set_framerate,
	.set_group_hold		= cam_set_group_hold,
	.fill_string_ctrl	= cam_fill_string_ctrl,
};

MODULE_DEVICE_TABLE(of, cam_of_match);

static struct camera_common_pdata *cam_parse_dt(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct cam *priv = (struct cam *)tc_dev->priv;
	struct device_node *node = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int err;
	const char *str;

	if (!node)
		return NULL;

	match = of_match_device(cam_of_match, dev);
	if (!match) {
		dev_err(dev, " Failed to find matching dt id\n");
		return NULL;
	}

	err = of_property_read_string(node, "use_sensor_mode_id", &str);
	if (!err) {
		if(!strcmp(str,"true"))
			priv->use_sensor_mode_id = true;
		else
			priv->use_sensor_mode_id = false;
	}

	board_priv_pdata = devm_kzalloc(dev,
			   sizeof(*board_priv_pdata), GFP_KERNEL);
	if (!board_priv_pdata) {
		dev_err(dev, "Failed to allocate pdata\n");
		return NULL;
	}

	err = of_property_read_string(node, "mclk",
				      &board_priv_pdata->mclk_name);
	if (err)
		dev_err(dev, "mclk not in DT\n");

	of_property_read_string(node, "avdd-reg",
			&board_priv_pdata->regulators.avdd);
	of_property_read_string(node, "dvdd-reg",
			&board_priv_pdata->regulators.dvdd);
	of_property_read_string(node, "iovdd-reg",
			&board_priv_pdata->regulators.iovdd);

	return board_priv_pdata;
}

static int cam_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops cam_subdev_internal_ops = {
	.open = cam_open,
};

static const struct media_entity_operations cam_media_ops = {
#ifdef CONFIG_MEDIA_CONTROLLER
	.link_validate = v4l2_subdev_link_validate,
#endif
};

static int cam_stream_config(struct i2c_client *client, struct cam *priv,
			uint32_t format, int mode, int frate_index)
{
        unsigned char mc_data[100];
        uint32_t payload_len = 0;

        uint16_t cmd_status = 0, index = 0xFFFF;
        uint8_t retcode = 0, cmd_id = 0;
        int loop = 0, ret = 0, err = 0, retry = 1000;

        /* lock semaphore */
        mutex_lock(&priv->cam_i2c_mutex);

        /* check current status of ISP */
        cmd_id = CMD_ID_STREAM_CONFIG;
        if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) < 0) {
                dev_err(&client->dev," %s(%d) MCU Init ISP Error \n", __func__, __LINE__);
                ret = -1;
                goto exit;
        }

        if ((cmd_status != MCU_CMD_STATUS_SUCCESS) ||
            (retcode == ERRCODE_BUSY)) {
                dev_err(&client->dev," ISP is Unintialized or Busy STATUS = 0x%04x Errcode = 0x%02x !! \n",
                     cmd_status, retcode);
                ret = -EBUSY;
                goto exit;
        }
        /* A failed previous STREAM_CONFIG leaves its error latched in the MCU
         * status register; treating it as fatal bricks every later attempt.
         * Only BUSY means the ISP can't take a new config now. */
        if (retcode != ERRCODE_SUCCESS)
                dev_warn(&client->dev, "stream_config: stale MCU retcode 0x%02x, sending fresh config\n",
                     retcode);

        /* call ISP Stream config command */
        for (loop = 0; (&priv->streamdb[loop])!= NULL; loop++) {
                if (priv->streamdb[loop] == mode) {
                        index = loop + frate_index;
                        break;
                }
        }

        if (index == 0xFFFF) {
                ret = -EINVAL;
                goto exit;
        }

        /* First Txn Payload length = 0 */
        payload_len = 14;

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_STREAM_CONFIG;
        mc_data[2] = payload_len >> 8;
        mc_data[3] = payload_len & 0xFF;
        mc_data[4] = errorcheck(&mc_data[2], 2);

        cam_write(client, mc_data, TX_LEN_PKT);

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_STREAM_CONFIG;
        mc_data[2] = index >> 8;
        mc_data[3] = index & 0xFF;

        mc_data[4] = format >> 24;
        mc_data[5] = format >> 16;
        mc_data[6] = format >> 8;
        mc_data[7] = format & 0xFF;

        /* width */
        mc_data[8] = priv->cam_frmfmt[mode].size.width >> 8;
        mc_data[9] = priv->cam_frmfmt[mode].size.width & 0xFF;

        /* height */
        mc_data[10] = priv->cam_frmfmt[mode].size.height >> 8;
        mc_data[11] = priv->cam_frmfmt[mode].size.height & 0xFF;

        /* frame rate num */
        mc_data[12] = priv->cam_frmfmt[mode].framerates[frate_index] >> 8;
        mc_data[13] = priv->cam_frmfmt[mode].framerates[frate_index] & 0xFF;

        /* frame rate denom */
        mc_data[14] = 0x00;
        mc_data[15] = 0x01;

        mc_data[16] = errorcheck(&mc_data[2], 14);
        dev_info(&client->dev, "stream_config: idx=%u fmt=0x%08x %ux%u@%u (mode=%d fidx=%d)\n",
                 index, format, priv->cam_frmfmt[mode].size.width,
                 priv->cam_frmfmt[mode].size.height,
                 priv->cam_frmfmt[mode].framerates[frate_index], mode, frate_index);
        err = cam_write(client, mc_data, 17);
        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Stream Config Error - %d \n", __func__,
                       __LINE__, err);
                ret = -1;
                goto exit;
        }

        while (--retry > 0) {
		/* test Some time for processing command */
                yield();

                cmd_id = CMD_ID_STREAM_CONFIG;
                if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) <
                    0) {
                       dev_err(&client->dev," %s(%d) MCU GET CMD Status Error : loop : %d \n", __func__,
                               __LINE__ , loop);
			ret = -1;
                        goto exit;
                }

                if ((cmd_status == MCU_CMD_STATUS_SUCCESS) &&
                    (retcode == ERRCODE_SUCCESS)) {
                        dev_dbg(&client->dev," %s %dISP GET CMD Status Succesfull !! \n", __func__, __LINE__);
			ret = 0;
                        goto exit;
                }

                if ((retcode != ERRCODE_BUSY) &&
                    ((cmd_status != MCU_CMD_STATUS_PENDING))) {
                       dev_err(&client->dev,
                            "(%s) %d ISP Get CMD Error STATUS = 0x%04x RET = 0x%02x\n",
                             __func__, __LINE__, cmd_status, retcode);
			ret = -1;
                        goto exit;
                }
		mdelay(1);
        }

 exit:
        /* unlock semaphore */
        mutex_unlock(&priv->cam_i2c_mutex);

        return ret;
}

static int cam_set_ctrl(struct i2c_client *client, struct cam *priv, uint32_t arg_ctrl_id,
                        uint8_t ctrl_type, int64_t curr_val)
{
        unsigned char mc_data[100];
        uint32_t payload_len = 0, ctrl_val_len = 0;

        uint16_t cmd_status = 0, index = 0xFFFF;
        uint8_t retcode = 0, cmd_id = 0;
        int loop = 0, ret = 0, err =0;
	int retry = 1000;

        /* lock semaphore */
        mutex_lock(&priv->cam_i2c_mutex);

        /* call ISP Ctrl config command */
        for (loop = 0; loop < priv->num_ctrls; loop++) {
                if (priv->ctrldb[loop] == arg_ctrl_id) {
                        index = loop;
                        break;
                }
        }
        if (index == 0xFFFF) {
                ret = -EINVAL;
                goto exit;
        }
	payload_len =
		(ctrl_type == CTRL_STANDARD) ? 11 : 20;
        /* First Txn Payload length = 0 */
	ctrl_val_len =
		(ctrl_type == CTRL_STANDARD) ? 4 : 8;


	mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_SET_CTRL;
        mc_data[2] = payload_len >> 8;
        mc_data[3] = payload_len & 0xFF;
        mc_data[4] = errorcheck(&mc_data[2], 2);

        cam_write(client, mc_data, TX_LEN_PKT);

        /* Second Txn */
        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_SET_CTRL;

        /* Index */
        mc_data[2] = index >> 8;
        mc_data[3] = index & 0xFF;

        /* Control ID */
        mc_data[4] = arg_ctrl_id >> 24;
        mc_data[5] = arg_ctrl_id >> 16;
        mc_data[6] = arg_ctrl_id >> 8;
        mc_data[7] = arg_ctrl_id & 0xFF;

        /* Ctrl Type */
        mc_data[8] = ctrl_type;

        /* Ctrl Value */
		if (ctrl_type == CTRL_STANDARD) {
			
			mc_data[9] = curr_val >> 24;
			mc_data[10] = curr_val >> 16;
			mc_data[11] = curr_val >> 8;
			mc_data[12] = curr_val & 0xFF;
			/* CRC */
			mc_data[13] = errorcheck(&mc_data[2], payload_len);

		} else {
			
			mc_data[9]  = V4L2_CTRL_TYPE_INTEGER64;
			mc_data[10] = ctrl_val_len >> 24;
		   	mc_data[11] = ctrl_val_len >> 16;
			mc_data[12] = ctrl_val_len >> 8;
			mc_data[13] = ctrl_val_len & 0xFF;
			for (loop = 0;loop < ctrl_val_len; loop++)
	   			mc_data[21-loop] = (curr_val >> (8 * loop));
        	/* CRC */
        	mc_data[22] = errorcheck(&mc_data[2], payload_len);
		}
        err = cam_write(client, mc_data, payload_len+3);
        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Set Ctrl Error - %d \n", __func__,
                       __LINE__, err);
                ret = -1;
                goto exit;
        }

        while (--retry > 0) {
		yield();
                cmd_id = CMD_ID_SET_CTRL;
                if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) <
                    0) {
                        dev_err(&client->dev," %s(%d) MCU Get CMD Status Error \n", __func__,
                               __LINE__);
                        ret = -1;
                        goto exit;
                }

                if ((cmd_status == MCU_CMD_STATUS_SUCCESS) &&
                    (retcode == ERRCODE_SUCCESS)) {
                        ret = 0;
                        goto exit;
                }

                if ((retcode != ERRCODE_BUSY) &&
                    ((cmd_status != MCU_CMD_STATUS_PENDING))) {
                       dev_err(&client->dev,
                           "(%s) %d MCU Get CMD Error STATUS = 0x%04x RET = 0x%02x\n",
                             __func__, __LINE__, cmd_status, retcode);
                        ret = -1;
                        goto exit;
                }
        }

 exit:
        /* unlock semaphore */
        mutex_unlock(&priv->cam_i2c_mutex);

        return ret;
}

static int cam_list_fmts(struct i2c_client *client, struct cam *priv,
			ISP_STREAM_INFO *stream_info,int *frm_fmt_size)
{
        /* MCU communication variables */
        unsigned char mc_data[100];
        unsigned char mc_ret_data[100];
        uint32_t payload_len = 0, err = 0;
        uint8_t errcode = ERRCODE_SUCCESS, orig_crc = 0, calc_crc = 0;
        uint16_t index = 0, mode = 0;

        int num_frates = 0, ret = 0, default_fmt_fourcc = 0;

        /* Stream Info Variables */

        /* lock semaphore */
        mutex_lock(&priv->cam_i2c_mutex);

        /* List all formats from MCU and append to cam_frmfmt array */

        for (index = 0;; index++) {
                /* First Txn Payload length = 0 */
                payload_len = 2;

                mc_data[0] = CMD_SIGNATURE;
                mc_data[1] = CMD_ID_GET_STREAM_INFO;
                mc_data[2] = payload_len >> 8;
                mc_data[3] = payload_len & 0xFF;
                mc_data[4] = errorcheck(&mc_data[2], 2);

                cam_write(client, mc_data, TX_LEN_PKT);

                mc_data[0] = CMD_SIGNATURE;
                mc_data[1] = CMD_ID_GET_STREAM_INFO;
                mc_data[2] = index >> 8;
                mc_data[3] = index & 0xFF;
                mc_data[4] = errorcheck(&mc_data[2], 2);
                err = cam_write(client, mc_data, 5);
                if (err != 0) {
                        dev_err(&client->dev," %s(%d) i2c error while writing command to MCU -%d \n", __func__,
                               __LINE__, err);
                        ret = -1;
                        goto exit;
                }

                err = cam_read(client, mc_ret_data, RX_LEN_PKT);
                if (err != 0) {
                        dev_err(&client->dev," %s(%d) i2c error while reading stream info. length from MCU - %d \n", __func__,
                               __LINE__, err);
                        ret = -1;
                        goto exit;
                }

                /* Verify CRC */
                orig_crc = mc_ret_data[4];
                calc_crc = errorcheck(&mc_ret_data[2], 2);
                if (orig_crc != calc_crc) {
                        dev_err(&client->dev," %s(%d)Checksum' mismatch in  MCU provided stream info. length: 0x%02x != 0x%02x \n",
                               __func__, __LINE__, orig_crc, calc_crc);
                        ret = -1;
                        goto exit;
                }
                if (((mc_ret_data[2] << 8) | mc_ret_data[3]) == 0) {
			if(stream_info == NULL) {
				*frm_fmt_size = index;
			} else {
	 			*frm_fmt_size = mode;
			}
                        break;
                }

                payload_len =
                    ((mc_ret_data[2] << 8) | mc_ret_data[3]) +
                    HEADER_FOOTER_SIZE;
                errcode = mc_ret_data[5];
                if (errcode != ERRCODE_SUCCESS) {
                        dev_err(&client->dev," %s(%d) MCU's return code has error set - 0x%02x \n",
                               __func__, __LINE__, errcode);
                        ret = -1;
                        goto exit;
                }

                memset(mc_ret_data, 0x00, payload_len);
                err = cam_read(client, mc_ret_data, payload_len);
                if (err != 0) {
                        dev_err(&client->dev," %s(%d) i2c error while reading actual stream info. - %d \n", __func__,
                               __LINE__, err);
                        ret = -1;
                        goto exit;
                }

                /* Verify CRC */
                orig_crc = mc_ret_data[payload_len - 2];
                calc_crc =
                    errorcheck(&mc_ret_data[2],
                                 payload_len - HEADER_FOOTER_SIZE);
                if (orig_crc != calc_crc) {
                        dev_err(&client->dev," %s(%d) Checksum' mismatch error in MCU provided stream info. : 0x%02x != 0x%02x \n",
                               __func__, __LINE__, orig_crc, calc_crc);
                        ret = -1;
                        goto exit;
                }

                /* Verify Errcode */
                errcode = mc_ret_data[payload_len - 1];
                if (errcode != ERRCODE_SUCCESS) {
                        dev_err(&client->dev," %s(%d) MCU's response has errcode set - 0x%02x \n",
                               __func__, __LINE__, errcode);
                        ret = -1;
                        goto exit;
                }
		if(stream_info != NULL) {
					/* check if any other format than UYVY is queried - do not append in array */
			stream_info->fmt_fourcc =
				mc_ret_data[2] << 24 | mc_ret_data[3] << 16 | mc_ret_data[4]
				<< 8 | mc_ret_data[5];
			if(index == 0)
				default_fmt_fourcc = stream_info->fmt_fourcc;
			stream_info->width = mc_ret_data[6] << 8 | mc_ret_data[7];
			stream_info->height = mc_ret_data[8] << 8 | mc_ret_data[9];
			stream_info->frame_rate_type = mc_ret_data[10];

			switch (stream_info->frame_rate_type) {
				case FRAME_RATE_DISCRETE:
					stream_info->frame_rate.disc.frame_rate_num =
						mc_ret_data[11] << 8 | mc_ret_data[12];

					stream_info->frame_rate.disc.frame_rate_denom =
						mc_ret_data[13] << 8 | mc_ret_data[14];

					break;

				case FRAME_RATE_CONTINOUS:
					dev_err(&client->dev,
							" The Stream format at index 0x%04x has FRAME_RATE_CONTINOUS,"
							"which is unsupported !! \n", index);

					continue;
			}
			dev_info(&client->dev,
				"MCU stream[%u]: fourcc=%c%c%c%c %ux%u rate_type=%u num=%u denom=%u\n",
				index,
				(stream_info->fmt_fourcc >> 24) & 0xff,
				(stream_info->fmt_fourcc >> 16) & 0xff,
				(stream_info->fmt_fourcc >> 8) & 0xff,
				stream_info->fmt_fourcc & 0xff,
				stream_info->width, stream_info->height,
				stream_info->frame_rate_type,
				stream_info->frame_rate.disc.frame_rate_num,
				stream_info->frame_rate.disc.frame_rate_denom);
			switch (stream_info->fmt_fourcc){
				case V4L2_PIX_FMT_SRGGB12:
				priv->cam_frmfmt[mode].size.width = stream_info->width;
				priv->cam_frmfmt[mode].size.height =
					stream_info->height;
				num_frates = priv->cam_frmfmt[mode].num_framerates;

				*((int *)(priv->cam_frmfmt[mode].framerates)+num_frates) =
					(int)(stream_info->frame_rate.disc.frame_rate_num /
							stream_info->frame_rate.disc.frame_rate_denom);
				priv->cam_frmfmt[mode].num_framerates++;
				priv->cam_frmfmt[mode].mode = mode;
				priv->streamdb[index] = mode;
				printk(KERN_INFO"stream mode : %d width : %d height : %d framerate : %d\n",priv->cam_frmfmt[mode].mode,stream_info->width,stream_info->height,priv->cam_frmfmt[mode].num_framerates);
				printk(KERN_INFO"stream_info->frame_rate.disc.frame_rate_num = %d --------------\n",stream_info->frame_rate.disc.frame_rate_num);
					mode++;
					break;
				case V4L2_PIX_FMT_SGBRG12:
				priv->cam_frmfmt[mode].size.width = stream_info->width;
				priv->cam_frmfmt[mode].size.height =
					stream_info->height;
				num_frates = priv->cam_frmfmt[mode].num_framerates;

				*((int *)(priv->cam_frmfmt[mode].framerates)+num_frates) =
					(int)(stream_info->frame_rate.disc.frame_rate_num /
							stream_info->frame_rate.disc.frame_rate_denom);
				priv->cam_frmfmt[mode].num_framerates++;
				priv->cam_frmfmt[mode].mode = mode;
				priv->streamdb[index] = mode;
				printk(KERN_INFO"stream mode : %d width : %d height : %d framerate : %d\n",priv->cam_frmfmt[mode].mode,stream_info->width,stream_info->height,priv->cam_frmfmt[mode].num_framerates);
				printk(KERN_INFO"stream_info->frame_rate.disc.frame_rate_num = %d --------------\n",stream_info->frame_rate.disc.frame_rate_num);
					mode++;
					break;

				case V4L2_PIX_FMT_SGBRG10:
				priv->cam_frmfmt[mode].size.width = stream_info->width;
				priv->cam_frmfmt[mode].size.height =
					stream_info->height;
				num_frates = priv->cam_frmfmt[mode].num_framerates;

				*((int *)(priv->cam_frmfmt[mode].framerates)+num_frates) =
					(int)(stream_info->frame_rate.disc.frame_rate_num /
							stream_info->frame_rate.disc.frame_rate_denom);
				priv->cam_frmfmt[mode].num_framerates++;
				priv->cam_frmfmt[mode].mode = mode;
				priv->streamdb[index] = mode;
				printk(KERN_INFO"stream mode : %d width : %d height : %d framerate : %d\n",priv->cam_frmfmt[mode].mode,stream_info->width,stream_info->height,priv->cam_frmfmt[mode].num_framerates);
				printk(KERN_INFO"stream_info->frame_rate.disc.frame_rate_num = %d --------------\n",stream_info->frame_rate.disc.frame_rate_num);
					mode++;
					break;

				case V4L2_PIX_FMT_SRGGB10:
				priv->cam_frmfmt[mode].size.width = stream_info->width;
				priv->cam_frmfmt[mode].size.height =
					stream_info->height;
				num_frates = priv->cam_frmfmt[mode].num_framerates;

				*((int *)(priv->cam_frmfmt[mode].framerates)+num_frates) =
					(int)(stream_info->frame_rate.disc.frame_rate_num /
							stream_info->frame_rate.disc.frame_rate_denom);
				priv->cam_frmfmt[mode].num_framerates++;
				priv->cam_frmfmt[mode].mode = mode;
				priv->streamdb[index] = mode;
				printk(KERN_INFO"stream mode : %d width : %d height : %d framerate : %d\n",priv->cam_frmfmt[mode].mode,stream_info->width,stream_info->height,priv->cam_frmfmt[mode].num_framerates);
				printk(KERN_INFO"stream_info->frame_rate.disc.frame_rate_num = %d --------------\n",stream_info->frame_rate.disc.frame_rate_num);
					mode++;
					break;

				case V4L2_PIX_FMT_SGRBG10:
				priv->cam_frmfmt[mode].size.width = stream_info->width;
				priv->cam_frmfmt[mode].size.height =
					stream_info->height;
				num_frates = priv->cam_frmfmt[mode].num_framerates;

				*((int *)(priv->cam_frmfmt[mode].framerates)+num_frates) =
					(int)(stream_info->frame_rate.disc.frame_rate_num /
							stream_info->frame_rate.disc.frame_rate_denom);
				priv->cam_frmfmt[mode].num_framerates++;
				priv->cam_frmfmt[mode].mode = mode;
				priv->streamdb[index] = mode;
				printk(KERN_INFO"stream mode : %d width : %d height : %d framerate : %d\n",priv->cam_frmfmt[mode].mode,stream_info->width,stream_info->height,priv->cam_frmfmt[mode].num_framerates);
				printk(KERN_INFO"stream_info->frame_rate.disc.frame_rate_num = %d --------------\n",stream_info->frame_rate.disc.frame_rate_num);
					mode++;
					break;

				default:
					dev_err(&client->dev, 
							" The Stream format at index 0x%04x has format 0x%08x ,"
							"which is unsupported \nSupported Formats are 0x%08x and 0x%08x!! \n", index,
							stream_info->fmt_fourcc, V4L2_PIX_FMT_SRGGB12,V4L2_PIX_FMT_SRGGB10);
			}

		}

        }

 exit:
        /* unlock semaphore */
        mutex_unlock(&priv->cam_i2c_mutex);

        return ret;

}


unsigned char errorcheck(char *data, unsigned int len)
{
        unsigned int i = 0;
        unsigned char crc = 0x00;

        for (i = 0; i < len; i++) {
                crc ^= data[i];
        }

        return crc;
}

static int cam_read(struct i2c_client *client, u8 * val, u32 count)
{
        int ret;
        struct i2c_msg msg = {
                .addr = client->addr,
                .flags = 0,
                .buf = val,
        };

        msg.flags = I2C_M_RD;
        msg.len = count;
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret < 0)
                goto err;

        return 0;

 err:
        dev_err(&client->dev, "Failed reading register ret = %d!\n", ret);
        return ret;
}

static int cam_write(struct i2c_client *client, u8 * val, u32 count)
{
        int ret;
        struct i2c_msg msg = {
                .addr = client->addr,
                .flags = 0,
                .len = count,
                .buf = val,
        };

        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret < 0) {
                dev_err(&client->dev, "Failed writing register ret = %d!\n",
                        ret);
                return ret;
        }

        return 0;
}

static int cam_get_cmd_status(struct i2c_client *client, uint8_t * cmd_id,
                              uint16_t * cmd_status, uint8_t * ret_code)
{
        unsigned char mc_data[100];
        unsigned char mc_ret_data[100];
        uint32_t payload_len = 0, err = 0;
        uint8_t orig_crc = 0, calc_crc = 0;


        /* First Txn Payload length = 0 */
        payload_len = 1;

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_GET_STATUS;
        mc_data[2] = payload_len >> 8;
        mc_data[3] = payload_len & 0xFF;
        mc_data[4] = errorcheck(&mc_data[2], 2);

        cam_write(client, mc_data, TX_LEN_PKT);

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_GET_STATUS;
        mc_data[2] = *cmd_id;
        err = cam_write(client, mc_data, 3);
        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Get CMD Status Write Error - %d \n", __func__,
                       __LINE__, err);
                return -1;
        }

        payload_len = CMD_STATUS_MSG_LEN;
        memset(mc_ret_data, 0x00, payload_len);
        err = cam_read(client, mc_ret_data, payload_len);
        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Get CMD Status Length Error - %d \n", __func__,
                       __LINE__, err);
                return -1;
        }

        /* Verify CRC */
        orig_crc = mc_ret_data[payload_len - 2];
        calc_crc = errorcheck(&mc_ret_data[2], 3);
        if (orig_crc != calc_crc) {
                dev_err(&client->dev," %s(%d) MCU Get CMD Status Error CRC 0x%02x != 0x%02x \n",
                       __func__, __LINE__, orig_crc, calc_crc);
                return -1;
        }

        *cmd_id = mc_ret_data[2];
        *cmd_status = mc_ret_data[3] << 8 | mc_ret_data[4];
        *ret_code = mc_ret_data[payload_len - 1];

        return 0;
}

static int cam_stream_on(struct i2c_client *client, struct cam *priv)
{
        unsigned char mc_data[100];
        uint32_t payload_len = 0;

        uint16_t cmd_status = 0;
        uint8_t retcode = 0, cmd_id = 0;
	int retry = 1000, err = 0;

        /* call ISP init command */
        /* lock semaphore */
        mutex_lock(&priv->cam_i2c_mutex);

        /* First Txn Payload length = 0 */
        payload_len = 0;

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_STREAM_ON;
        mc_data[2] = payload_len >> 8;
        mc_data[3] = payload_len & 0xFF;
        mc_data[4] = errorcheck(&mc_data[2], 2);

        cam_write(client, mc_data, TX_LEN_PKT);

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_STREAM_ON;
        err = cam_write(client, mc_data, 2);
        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Stream On Write Error - %d \n", __func__,
                       __LINE__, err);
                goto exit;
        }

        while (--retry > 0) {
                /* Some Sleep for init to process */
                yield();

                cmd_id = CMD_ID_STREAM_ON;
                if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) <
                    0) {
                       dev_err(&client->dev," %s(%d) MCU Get CMD Stream On Error \n", __func__,
                               __LINE__);
		       err = -1;
                        goto exit;
                }

                if ((cmd_status == MCU_CMD_STATUS_SUCCESS) &&
                    (retcode == ERRCODE_SUCCESS)) {
                        dev_dbg(&client->dev," %s %dMCU Stream On Success !! \n", __func__, __LINE__);
			err = 0;
                        goto exit;
                }

                if ((retcode != ERRCODE_BUSY) &&
                    ((cmd_status != MCU_CMD_STATUS_PENDING))) {
                       dev_err(&client->dev,
                            "(%s) %d MCU Get CMD Stream On Error STATUS = 0x%04x RET = 0x%02x\n",
                             __func__, __LINE__, cmd_status, retcode);
		       err = -1;
                        goto exit;
                }
		mdelay(1);
        }
 exit:
       	/* unlock semaphore */
	        mutex_unlock(&priv->cam_i2c_mutex);
		return err;

}



static int cam_stream_off(struct i2c_client *client, struct cam *priv)
{
        unsigned char mc_data[100];
        uint32_t payload_len = 0;

        uint16_t cmd_status = 0;
        uint8_t retcode = 0, cmd_id = 0;
	int retry = 1000, err = 0;
        /* call ISP init command */

	/*lock semaphore*/
        mutex_lock(&priv->cam_i2c_mutex);

        /* First Txn Payload length = 0 */
        payload_len = 0;

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_STREAM_OFF;
        mc_data[2] = payload_len >> 8;
        mc_data[3] = payload_len & 0xFF;
        mc_data[4] = errorcheck(&mc_data[2], 2);

        cam_write(client, mc_data, TX_LEN_PKT);

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_STREAM_OFF;
        err = cam_write(client, mc_data, 2);
        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Stream OFF Write Error - %d \n", __func__,
                       __LINE__, err);
                goto exit;
        }

        while (--retry > 0) {
                /* Some Sleep for init to process */
                yield();

                cmd_id = CMD_ID_STREAM_OFF;
                if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) <
                    0) {
                       dev_err(&client->dev," %s(%d) MCU Get CMD Stream Off Error \n", __func__,
                               __LINE__);
		       err = -1;
                        goto exit;
                }

                if ((cmd_status == MCU_CMD_STATUS_SUCCESS) &&
                    (retcode == ERRCODE_SUCCESS)) {
                        dev_dbg(&client->dev," %s %d MCU Get CMD Stream off Success !! \n", __func__, __LINE__ );
			err = 0;
                        goto exit;
                }

                if ((retcode != ERRCODE_BUSY) &&
                    ((cmd_status != MCU_CMD_STATUS_PENDING))) {
                       dev_err(&client->dev,
                            "(%s) %d MCU Get CMD Stream off Error STATUS = 0x%04x RET = 0x%02x\n",
                             __func__, __LINE__, cmd_status, retcode);
		       err = -1;
                        goto exit;
                }
		mdelay(1);
        }
exit:
	/* unlock semaphore */
	mutex_unlock(&priv->cam_i2c_mutex);
	return err;
}

int cam_bload_get_version(struct i2c_client *client)
{
        int ret = 0;
        /*----------------------------- GET VERSION -------------------- */

        /*   Write Get Version CMD */
        g_bload_buf[0] = BL_GET_VERSION;
        g_bload_buf[1] = ~(BL_GET_VERSION);

        ret = cam_write(client, g_bload_buf, 2);
        if (ret < 0) {
                dev_err(&client->dev,"Write Failed \n");
                return -1;
        }

        /*   Wait for ACK or NACK */
        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Read Failed \n");
                return -1;
        }

        if (g_bload_buf[0] != 'y') {
                /*   NACK Received */
                dev_err(&client->dev," NACK Received... exiting.. \n");
                return -1;
        }

        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Read Failed \n");
                return -1;
        }

        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Read Failed\n");
                return -1;
        }

        /* ---------------- GET VERSION END ------------------- */

        return 0;
}
static int detect_camera(struct i2c_client *client, uint16_t reset_gpio, uint16_t boot_gpio)
{
	int ret = 0;

	gpio_set_output_value(boot_gpio, 0);
	gpio_set_output_value(reset_gpio, 0);
	msleep(1);
	gpio_set_output_value(boot_gpio, 1);
	msleep(1);
	gpio_set_output_value(reset_gpio, 1);
	msleep(10);

	if((ret = cam_bload_get_version(client)) < 0 )
       	{
		dev_dbg(&client->dev, "Unable to initiate i2c communication. Device not detected\n");
		return ret;
	}
	dev_dbg(&client->dev, "Camera Device detected\n");

	return ret;
}


static int cam_lane_configuration(struct i2c_client *client, struct cam *priv)
{
	int ret = 0, err;
	uint16_t payload_data;
        unsigned char mc_data[10];
        uint32_t payload_len = 0;
        uint16_t cmd_status = 0;
        uint8_t retcode = 0, cmd_id = 0;
	int retry = 1000;

        /* lock semaphore */
        mutex_lock(&priv->cam_i2c_mutex);

	payload_len = 2;

	mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_LANE_CONFIG;
        mc_data[2] = payload_len >> 8;
        mc_data[3] = payload_len & 0xFF;
        mc_data[4] = errorcheck(&mc_data[2], 2);

        cam_write(client, mc_data, TX_LEN_PKT);

        /* Second Txn */
        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_LANE_CONFIG;

        /* Lane Configuration */
	payload_data = priv->mipi_lane_config == 4 ? NUM_LANES_4 : NUM_LANES_2;
        mc_data[2] = payload_data >> 8;
        mc_data[3] = payload_data & 0xff;

       	/* CRC */
       	mc_data[4] = errorcheck(&mc_data[2], payload_len);
        err = cam_write(client, mc_data, payload_len+3);

        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Set Ctrl Error - %d \n", __func__,
                       __LINE__, err);
                ret = -1;
                goto exit;
        }

	while (--retry > 0) {
		yield();
                cmd_id = CMD_ID_LANE_CONFIG;
                if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) <
                    0) {
                        dev_err(&client->dev," %s(%d) MCU Get CMD Status Error \n", __func__,
                               __LINE__);
                        ret = -1;
                        goto exit;
                }

                if ((cmd_status == MCU_CMD_STATUS_ISP_UNINIT) &&
                    (retcode == ERRCODE_SUCCESS)) {
                        ret = 0;
                        goto exit;
                }

                if ((retcode != ERRCODE_BUSY) &&
                    ((cmd_status != MCU_CMD_STATUS_ISP_UNINIT))) {
                       dev_err(&client->dev,
                           "(%s) %d MCU Get CMD Error STATUS = 0x%04x RET = 0x%02x\n",
                             __func__, __LINE__, cmd_status, retcode);
                        ret = -1;
                        goto exit;
                }
        }

 exit:
        /* unlock semaphore */
        mutex_unlock(&priv->cam_i2c_mutex);

        return ret;
}
static int ecam_mcu_firmware_load(struct i2c_client *client)
{
        unsigned char fw_version[32] = {0}, txt_fw_version[32] = {0};
        int i= 0,loop = 0, ret = 0;
        unsigned long txt_fw_pos = 0;
        /* Request firmware from the rootfs */
        ret = request_firmware(&mcu_fw, mcu_fw_name, &client->dev);
        if(ret < 0)
                return -ENOENT;
        txt_fw_pos = mcu_fw->size - VERSION_FILE_OFFSET;
        mcu_fw_buf = kmalloc(mcu_fw->size+1, GFP_KERNEL);
        mcu_fw_buf[mcu_fw->size] = '\0';
        memcpy(mcu_fw_buf, mcu_fw->data, mcu_fw->size);

        return ERRCODE_SUCCESS;
}



static int cam_core_initialize(struct tegracam_device *tc_dev)
{
	struct cam *priv = (struct cam *)tc_dev->priv;
	struct device *dev = tc_dev->dev;
	struct device_node *node = dev->of_node;
	int ret, loop;
	int frm_fmt_size = 0;
	uint32_t lanes=0;
	int rgpio,bgpio,fgpio;
	int retry = 5;
	unsigned char fw_version[32] = {0}, bin_fw_version[32] ={0};

        if (!IS_ENABLED(CONFIG_OF) || !dev->of_node)
		return -EINVAL;

	rgpio = of_get_named_gpio(node, "reset-gpios", 0);
	if (rgpio < 0) {
		dev_err(dev, "Unable to get Reset GPIO\n");
		return -EINVAL;
	}
	printk("Reset GPIO: %d\n",  rgpio);
	priv->reset_gpio = (unsigned int)rgpio;

	bgpio = of_get_named_gpio(node, "boot-gpios", 0);
	if (bgpio < 0) {
	    	dev_err(dev, "Unable to get Boot GPIO\n");
		return -EINVAL;
	}
	printk("Boot GPIO: %d\n",  bgpio);
	priv->boot_gpio = (unsigned int)bgpio;

	if(priv->sensor_info->flash_gpios_used) {
		fgpio = of_get_named_gpio(node, "flash-gpios", 0);
		if (fgpio < 0) {
			dev_err(dev, "Unable to get Flash GPIO\n");
			return -EINVAL;
		}
		priv->flash_gpio = (unsigned int)fgpio;
	}

	gpio_set_output_value(priv->reset_gpio, 0);
	gpio_set_output_value(priv->boot_gpio, 0);
	msleep(10);
	gpio_set_output_value(priv->reset_gpio, 1);
	if(priv->sensor_info->flash_gpios_used)
		gpio_set_output_value(priv->flash_gpio, 1);
	msleep(100);

	ret = of_property_read_u32(node, "camera_mipi_lanes", &lanes);
	if (ret < 0) {
	    dev_err(dev, "Error in getting Camera MIPI Lanes\n");
	    return -EINVAL;
	}
	priv->mipi_lane_config = lanes;
	// Read MCU firmware bin name from device tree
	ret = of_property_read_string(node, "mcu_fw_name",&mcu_fw_name);
        if(!ret){
                pr_info("Firmware name from the device tree is > %s\n",
                                			mcu_fw_name);
	}
        else{
                pr_info("Unable to get mcu firmware name form Device tree\n");
		return -EINVAL;
	}
	
	// Check if the MCU firmware is loaded or not
	// If not loaded already, load the MCU firmware
	if(!is_fw_loaded) {
		pr_info("Inside mcu_firmware load");
		if(ecam_mcu_firmware_load(tc_dev->client) < ERRCODE_SUCCESS){
			dev_err(dev,"failed to load mcu firmware\n");
				return -ENOENT;
		}
		else{
			pr_info("Firmware Successfully read by driver\n");
			is_fw_loaded = 1;
		}
	}
	
	
	// Initialize Mutex
	mutex_init(&priv->cam_i2c_mutex);

	// Detecting Presence of MCU
	if(detect_camera(tc_dev->client,priv->reset_gpio, priv->boot_gpio) < 0)
	{
		dev_dbg(dev,"Unable to initialize camera\n");
		return -ENODEV;
	}
	gpio_set_output_value(priv->reset_gpio, 0);
 	gpio_set_output_value(priv->boot_gpio, 0);
	if(priv->sensor_info->flash_gpios_used)
		gpio_set_output_value(priv->flash_gpio, 0);
 	msleep(1);
 	gpio_set_output_value(priv->reset_gpio, 1);
	if(priv->sensor_info->flash_gpios_used)
	 	gpio_set_output_value(priv->flash_gpio, 1);
	msleep(100);

	if ((ret = is_fw_update_required(tc_dev->client, priv, fw_version, bin_fw_version)) < 0) {
		if (ret == -2) {
			dev_dbg(dev,"Forced MCU FW Update field set. Switching MCU to bootloader mode for FW Update...\n");
		} else {
			/* ret value has to be -1 */
			dev_err(dev," Switching MCU to Bootloadermode \n");
		}

		ret = cam_bload_get_version(tc_dev->client);
		if (ret < 0) {
			dev_err(dev," Error in Get Version \n");

			/* Since error in reading the bootloader version: set MCU to bootloader mode */
			gpio_set_output_value(priv->reset_gpio, 0);
			msleep(1);
			gpio_set_output_value(priv->boot_gpio, 1);
			msleep(1);
			gpio_set_output_value(priv->reset_gpio, 1);
			msleep(10);

			/* Reading the MCU Firmware version from bootloader mode */
			for(loop = 0;loop < MAX_ATTEMPTS; loop++) {
				ret = cam_bload_get_version(tc_dev->client);
				if (ret < 0) {
					dev_dbg(dev, "Error getting Firmware version.. Retrying...\n");
					msleep(1000);
					continue;
				} else {
					break;
				}
			}

			/* Failed reading FW version in bootloader mode even after MAX_attempts. Return Failure */
			if (loop == MAX_ATTEMPTS) {
				dev_err(dev, "%s (%d) Error in reading MCU FW version in bootloader mode also. Exiting. \n", __func__, __LINE__);
				return -EINVAL;
			}
		}

		/*Attempt Firmware Update */
		if (cam_fw_update(tc_dev->client,bin_fw_version) < 0) {
			dev_err(dev, "%s (%d) Error Updating MCU FW. Exiting. \n", __func__, __LINE__);
			return -EFAULT;
		}

			/* Allow FW Updated Driver to reboot */
			msleep(100);
			gpio_set_output_value(priv->boot_gpio, 0);
			msleep(100);
 			gpio_set_output_value(priv->reset_gpio, 0);
			msleep(10);
 			gpio_set_output_value(priv->reset_gpio, 1);
			msleep(100);

	} else {
		/* Same Firmware version in MCU and text file */
		dev_dbg(dev,"Same firmware in MCU & text file - (%c_%c_%.12s_%c_%c_%c_%c_%.7s). Skipping FW Update...\n",
				fw_version[0], fw_version[1], &fw_version[2], fw_version[14], fw_version[15], fw_version[16], fw_version[17], &fw_version[18]);
	}


	msleep(100);

		/* Configure MIPI Lanes of the Sensor */
	retry = 5;
	while (--retry > 0) {
		if (cam_lane_configuration(tc_dev->client, priv) < 0) {
			dev_err(dev, "%s, Failed to set lane CONFIG Data. retrying!\n",__func__);
			continue;
		} else {
			break;
		}
	}
	if (retry < 0) {
		dev_err(dev, "%s, Failed to set lane CONFIG Data!\n",__func__);
		return -EFAULT;
	}

	/* Query the number of controls from MCU */
	retry = 5;
	while (--retry > 0) {
		if(cam_list_ctrls(tc_dev->client, priv, NULL) < 0) {
			dev_err(dev,"%s, init controls failure. retrying\n",__func__);
			continue;
		} else {
			break;
		}
	}
	if (retry < 0) {
		dev_err(dev, "%s, Failed to init controls!\n",__func__);
		return -EFAULT;
	}

	priv->cam_ctrl_info = devm_kzalloc(dev, sizeof(ISP_CTRL_INFO) * priv->num_ctrls, GFP_KERNEL);
	if(!priv->cam_ctrl_info) {
		dev_err(dev,"Unable to allocate memory!\n");
		return -ENOMEM;
	}

	priv->ctrldb = devm_kzalloc(dev, sizeof(uint32_t) * priv->num_ctrls , GFP_KERNEL);
	if (!priv->ctrldb) {
		dev_err(dev,"Unable to allocate memory!\n");
		return -ENOMEM;
	}

	/* Fill the controls */
	retry = 5;
	while (--retry > 0) {
		if(cam_list_ctrls(tc_dev->client, priv, priv->cam_ctrl_info) < 0) {
			dev_err(dev,"%s, Failed to init controls\n",__func__);
		} else {
			dev_dbg(dev, "Num of Controls - %d\n", priv->num_ctrls);
			break;
		}
	}
	if (retry < 0) {
		dev_err(dev, "%s, Failed to init formats!\n",__func__);
		return -EFAULT;
	}

	/* Query the number of formats available from MCU */
	retry = 5;
	while (--retry > 0) {
		if(cam_list_fmts(tc_dev->client, priv, NULL, &frm_fmt_size) < 0) {
			dev_err(dev,"%s, Failed to init formats\n",__func__);
			continue;
		} else {
			tc_dev->sensor_ops->numfrmfmts 	  = frm_fmt_size;
			break;
		}
	}
	if (retry < 0) {
		dev_err(dev, "%s, Failed to init formats!\n",__func__);
		return -EFAULT;
	}

	priv->stream_info = devm_kzalloc (dev, sizeof(ISP_STREAM_INFO) * (frm_fmt_size+1), GFP_KERNEL);
	priv->streamdb = devm_kzalloc(dev, sizeof(int) * (frm_fmt_size+1), GFP_KERNEL);
	if(!priv->streamdb ) {
		dev_err(dev, "unable to allocate memory\n");
		return -ENOMEM;
	}

	priv->cam_frmfmt = devm_kzalloc(dev, sizeof(struct camera_common_frmfmt) * (frm_fmt_size + 1) ,GFP_KERNEL);
	if(!priv->cam_frmfmt ) {
		dev_err(dev,"Unable to allocate memory\n");
		return -ENOMEM;
	}

	/* Initialise the ISP */
	if (cam_init(tc_dev->client) < 0) {
                dev_err(dev, "Unable to INIT ISP \n");
                return -EFAULT;
        }

	for (loop = 0; loop <= (frm_fmt_size); loop++) {
		/* create Frame Rate array */
		priv->cam_frmfmt[loop].framerates = devm_kzalloc (dev, sizeof(int) * MAX_NUM_FRATES, GFP_KERNEL);
		if (!priv->cam_frmfmt[loop].framerates) {
			dev_err(dev,"Unable to create memory\n");
			return -ENOMEM;
		}
	}

	/* List the formats from MCU */
	retry = 5;
	while (--retry > 0) {
		if (cam_list_fmts(tc_dev->client, priv, priv->stream_info,&frm_fmt_size) < 0) {
	                dev_err(dev, "Unable to List Fmts. retrying! \n");
			continue;
	        } else {
			tc_dev->sensor_ops->frmfmt_table  = priv->cam_frmfmt;
			break;
		}
	}
	if (retry < 0) {
		dev_err(dev, "%s, Failed to List formats!\n",__func__);
		return -EFAULT;
	}

	return 0;
}

static struct camera_common_sensor_ops cam_common_ops = {
	.power_on = cam_power_on,
	.power_off = cam_power_off,
	.write_reg = cam_write_reg,
	.read_reg = cam_read_reg,
	.parse_dt = cam_parse_dt,
	.power_get = cam_power_get,
	.power_put = cam_power_put,
	.set_mode = cam_set_mode,
	.start_streaming = cam_start_streaming,
	.stop_streaming = cam_stop_streaming,
};


static int cam_init(struct i2c_client *client)
{
        unsigned char mc_data[100];
        uint32_t payload_len = 0;

        uint16_t cmd_status = 0;
        uint8_t retcode = 0, cmd_id = 0;
	int retry = 1000, err = 0 ,ret = 0;

        /* check current status of ISP */
        cmd_id = CMD_ID_INIT_CAM;
        if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) < 0) {
                dev_err(&client->dev," %s(%d) MCU CAM Init ISP Error \n", __func__, __LINE__);
                return -1;
        }

        if ((cmd_status == MCU_CMD_STATUS_SUCCESS) &&
            (retcode == ERRCODE_SUCCESS)) {
                dev_dbg(&client->dev," %s %d MCU CAM Initialized !! \n", __func__, __LINE__ );
                return 0;
        }

        /* call ISP init command */

        /* First Txn Payload length = 0 */
        payload_len = 0;

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_INIT_CAM;
        mc_data[2] = payload_len >> 8;
        mc_data[3] = payload_len & 0xFF;
        mc_data[4] = errorcheck(&mc_data[2], 2);

        cam_write(client, mc_data, TX_LEN_PKT);

        mc_data[0] = CMD_SIGNATURE;
        mc_data[1] = CMD_ID_INIT_CAM;
        err = cam_write(client, mc_data, 2);
        if (err != 0) {
                dev_err(&client->dev," %s(%d) MCU Get CMD CAM Init Error - %d \n", __func__,
                       __LINE__, err);
                return -1;
        }

        while (--retry > 0) {
                /* Some Sleep for init to process */

                cmd_id = CMD_ID_INIT_CAM;
                if (cam_get_cmd_status(client, &cmd_id, &cmd_status, &retcode) <
                    0) {
                       dev_err(&client->dev," %s(%d) MCU CMD ID CAM INIT Error \n", __func__,
                               __LINE__);
                        msleep(5);
			ret = -1;
			continue;
                }

                if ((cmd_status == MCU_CMD_STATUS_SUCCESS) &&
                    (retcode == ERRCODE_SUCCESS)) {
                        dev_err(&client->dev,"%s %dMCU CMD ID CAM INIT Success !! \n", __func__,__LINE__);
			ret = 0;
			goto exit_init;
                }

                if ((retcode != ERRCODE_BUSY) &&
                    ((cmd_status != MCU_CMD_STATUS_PENDING))) {
                       dev_err(&client->dev,
                            "(%s) %d MCU CMD ID CAM INIT Error STATUS = 0x%04x RET = 0x%02x\n",
                             __func__, __LINE__, cmd_status, retcode);
		       ret = -1;
                        continue;
                }
		mdelay(10);
                yield();
        }
exit_init:
	return ret;
}

static int cam_list_ctrls(struct i2c_client *client, struct cam *priv,
                          ISP_CTRL_INFO * cam_ctrl_info)
{
        /* MCU communication variables */
        unsigned char mc_data[100];
        unsigned char mc_ret_data[100];
        uint32_t payload_len = 0;
        uint8_t errcode = ERRCODE_SUCCESS, orig_crc = 0, calc_crc = 0;
        uint16_t index = 0;
        int ret = 0, err =0,i;
	int retry = 1000;

        /* lock semaphore */
        mutex_lock(&priv->cam_i2c_mutex);

        /* Array of Ctrl Info */
        while (--retry > 0) {
                /* First Txn Payload length = 0 */
                payload_len = 2;

                mc_data[0] = CMD_SIGNATURE;
                mc_data[1] = CMD_ID_GET_CTRL_INFO;
                mc_data[2] = payload_len >> 8;
                mc_data[3] = payload_len & 0xFF;
                mc_data[4] = errorcheck(&mc_data[2], 2);

                cam_write(client, mc_data, TX_LEN_PKT);

                mc_data[0] = CMD_SIGNATURE;
                mc_data[1] = CMD_ID_GET_CTRL_INFO;
                mc_data[2] = index >> 8;
                mc_data[3] = index & 0xFF;
                mc_data[4] = errorcheck(&mc_data[2], 2);
                err = cam_write(client, mc_data, 5);
                if (err != 0) {
                        dev_err(&client->dev," %s(%d) MCU CMD ID CTRLS Write Error - %d \n", __func__,
                               __LINE__, err);
                        ret = -1;
                        goto exit;
                }

                err = cam_read(client, mc_ret_data, RX_LEN_PKT);
                if (err != 0) {
                        dev_err(&client->dev," %s(%d) MCU CMD ID List Ctrls Error - %d \n", __func__,
                               __LINE__, err);
                        ret = -1;
                        goto exit;
                }

                /* Verify CRC */
                orig_crc = mc_ret_data[4];
                calc_crc = errorcheck(&mc_ret_data[2], 2);
                if (orig_crc != calc_crc) {
                        dev_err(&client->dev, " %s(%d) MCU CMD ID List Ctrls Error CRC 0x%02x != 0x%02x \n",
                               __func__, __LINE__, orig_crc, calc_crc);
                        ret = -1;
                        goto exit;
                }

                if (((mc_ret_data[2] << 8) | mc_ret_data[3]) == 0) {
                        priv->num_ctrls = index;
                        break;
                }

                payload_len =
                    ((mc_ret_data[2] << 8) | mc_ret_data[3]) +
                    HEADER_FOOTER_SIZE;
                errcode = mc_ret_data[5];
                if (errcode != ERRCODE_SUCCESS) {
                        dev_err(&client->dev," %s(%d) MCU CMD ID List Ctrls Errcode - 0x%02x \n",
                               __func__, __LINE__, errcode);
                        ret = -1;
                        goto exit;
                }

                memset(mc_ret_data, 0x00, payload_len);
                err = cam_read(client, mc_ret_data, payload_len);
                if (err != 0) {
                       dev_err(&client->dev," %s(%d) MCU CMD ID List Ctrls Read Error - %d \n", __func__,
                               __LINE__, err);
                        ret = -1;
                        goto exit;
                }

                /* Verify CRC */
                orig_crc = mc_ret_data[payload_len - 2];
                calc_crc =
                    errorcheck(&mc_ret_data[2],
                                 payload_len - HEADER_FOOTER_SIZE);
                if (orig_crc != calc_crc) {
                        dev_err(&client->dev," %s(%d) MCU CMD ID List Ctrls Error CRC 0x%02x != 0x%02x \n",
                               __func__, __LINE__, orig_crc, calc_crc);
                        ret = -1;
                        goto exit;
                }

                /* Verify Errcode */
                errcode = mc_ret_data[payload_len - 1];
                if (errcode != ERRCODE_SUCCESS) {
                        dev_err(&client->dev," %s(%d) MCU CMD ID List Ctrls Errcode - 0x%02x \n",
                               __func__, __LINE__, errcode);
                        ret = -1;
                        goto exit;
                }
		if(cam_ctrl_info != NULL) {
                /* append ctrl info in array */
                cam_ctrl_info[index].ctrl_id =
                    mc_ret_data[2] << 24 | mc_ret_data[3] << 16 | mc_ret_data[4]
                    << 8 | mc_ret_data[5];
                cam_ctrl_info[index].ctrl_type = mc_ret_data[6];
                switch (cam_ctrl_info[index].ctrl_type) {
                case CTRL_STANDARD:
                        cam_ctrl_info[index].ctrl_data.std.ctrl_min =
                            mc_ret_data[7] << 24 | mc_ret_data[8] << 16 |
                            mc_ret_data[9] << 8 | mc_ret_data[10];

                        cam_ctrl_info[index].ctrl_data.std.ctrl_max =
                            mc_ret_data[11] << 24 | mc_ret_data[12] << 16 |
                            mc_ret_data[13]
                            << 8 | mc_ret_data[14];

                        cam_ctrl_info[index].ctrl_data.std.ctrl_def =
                            mc_ret_data[15] << 24 | mc_ret_data[16] << 16 |
                            mc_ret_data[17]
                            << 8 | mc_ret_data[18];

                        cam_ctrl_info[index].ctrl_data.std.ctrl_step =
                            mc_ret_data[19] << 24 | mc_ret_data[20] << 16 |
                            mc_ret_data[21]
                            << 8 | mc_ret_data[22];
                        break;

                case CTRL_EXTENDED:
					cam_ctrl_info[index].ctrl_data.ext.val_type = mc_ret_data[7];
					cam_ctrl_info[index].ctrl_data.ext.val_length =
						mc_ret_data[8] << 24 | mc_ret_data[9] << 16 |
							mc_ret_data[10] << 8 | mc_ret_data[11];
						for(i = 0 ; i < cam_ctrl_info[index].ctrl_data.ext.val_length ; i++)
							cam_ctrl_info[index].ctrl_data.ext.val_data[i] = mc_ret_data[12+i];
						if (cam_ctrl_info[index].ctrl_data.ext.val_type == V4L2_CTRL_TYPE_INTEGER64) {
							for (i = 0; i < EXTENDED_CTRL_SIZE; i++) {
								cam_ctrl_info[index].ctrl_data.ext.ctrl_min |=
									cam_ctrl_info[index].ctrl_data.ext.val_data[i] << 8 * (7-i);
								cam_ctrl_info[index].ctrl_data.ext.ctrl_max |=
									cam_ctrl_info[index].ctrl_data.ext.val_data[8+i] << 8 * (7-i);
								cam_ctrl_info[index].ctrl_data.ext.ctrl_def |=
									cam_ctrl_info[index].ctrl_data.ext.val_data[16+i] << 8 * (7-i);
								cam_ctrl_info[index].ctrl_data.ext.ctrl_step |=
									cam_ctrl_info[index].ctrl_data.ext.val_data[24+i] << 8 * (7-i);
							}
						} else if(cam_ctrl_info[index].ctrl_data.ext.val_type == V4L2_CTRL_TYPE_STRING) {
							for (i = 0; i < EXTENDED_CTRL_SIZE; i++) {
								cam_ctrl_info[index].ctrl_data.ext.ctrl_min |=
									cam_ctrl_info[index].ctrl_data.ext.val_data[i] << 8 * (7-i);
								cam_ctrl_info[index].ctrl_data.ext.ctrl_max |=
									cam_ctrl_info[index].ctrl_data.ext.val_data[8+i] << 8 * (7-i);
								cam_ctrl_info[index].ctrl_data.ext.ctrl_step |=
									cam_ctrl_info[index].ctrl_data.ext.val_data[24+i] << 8 * (7-i);
							}
						}
					break;

				}
                priv->ctrldb[index] = cam_ctrl_info[index].ctrl_id;
	}
                index++;
        }

 exit:
        /* unlock semaphore */
        mutex_unlock(&priv->cam_i2c_mutex);

        return ret;

}

int cam_bload_ascii2hex(unsigned char ascii)
{
        if (ascii <= '9') {
                return (ascii - '0');
        } else if ((ascii >= 'a') && (ascii <= 'f')) {
                return (0xA + (ascii - 'a'));
        } else if ((ascii >= 'A') && (ascii <= 'F')) {
                return (0xA + (ascii - 'A'));
        }

        return -1;
}

static int is_fw_update_required(struct i2c_client *client, struct cam *priv,
			unsigned char *fw_version, unsigned char *bin_fw_version)
{
        unsigned char mc_data[1024];
        unsigned char mc_ret_data[1024];
        uint32_t payload_len = 0, err = 0;
	unsigned long bin_fw_pos = strlen(mcu_fw_buf)-VERSION_FILE_OFFSET;
        uint8_t errcode = ERRCODE_SUCCESS, orig_crc = 0, calc_crc = 0;
        int ret = 0,loop,i=0;

        /* lock semaphore */
        mutex_lock(&priv->cam_i2c_mutex);

		for(loop=bin_fw_pos; loop < (bin_fw_pos+64); loop=loop+2) {
			*(bin_fw_version+i) = (cam_bload_ascii2hex(mcu_fw_buf[loop]) << 4 |
				cam_bload_ascii2hex(mcu_fw_buf[loop+1]));
		i++;
	}

	/* Check for forced/always update field in the text firmware version */

		if (bin_fw_version[17] == '1') {
			dev_err(&client->dev,"Debug Flag Enabled - Firmware Version - (%c_%c_%.12s_%c_%c_%c_%c-%.7s) \n"
				,bin_fw_version[0], bin_fw_version [1], &bin_fw_version[2],
				bin_fw_version[14], bin_fw_version [15], bin_fw_version[16], bin_fw_version [17], &bin_fw_version[18]);
			ret = -2;
			goto exit;

		} else {

			/* Query firmware version from MCU */
			payload_len = 0;

			mc_data[0] = CMD_SIGNATURE;
			mc_data[1] = CMD_ID_VERSION;
			mc_data[2] = payload_len >> 8;
			mc_data[3] = payload_len & 0xFF;
			mc_data[4] = errorcheck(&mc_data[2], 2);

			cam_write(client, mc_data, TX_LEN_PKT);
			mc_data[0] = CMD_SIGNATURE;
			mc_data[1] = CMD_ID_VERSION;
			err = cam_write(client, mc_data, 2);
			if (err != 0) {
				dev_err(&client->dev," %s(%d) MCU CMD ID Write PKT fw Version Error - %d \n", __func__,
						__LINE__, err);
				ret = -1;
				goto exit;
			}

			err = cam_read(client, mc_ret_data, RX_LEN_PKT);
			if (err != 0) {
				dev_err(&client->dev," %s(%d) MCU CMD ID Read PKT fw Version Error - %d \n", __func__,
						__LINE__, err);
				ret = -1;
				goto exit;
			}

			/* Verify checksum' */
			orig_crc = mc_ret_data[4];
			calc_crc = errorcheck(&mc_ret_data[2], 2);
			if (orig_crc != calc_crc) {
				dev_err(&client->dev," %s(%d) MCU CMD ID fw Version Error CRC 0x%02x != 0x%02x \n",
						__func__, __LINE__, orig_crc, calc_crc);
				ret = -1;
				goto exit;
			}

			errcode = mc_ret_data[5];
			if (errcode != ERRCODE_SUCCESS) {
				dev_err(&client->dev," %s(%d) MCU CMD ID fw Errcode - 0x%02x \n", __func__,
						__LINE__, errcode);
				ret = -1;
				goto exit;
			}

			/* Read the actual version from MCU */
			payload_len =
				((mc_ret_data[2] << 8) | mc_ret_data[3]) + HEADER_FOOTER_SIZE;
			memset(mc_ret_data, 0x00, payload_len);
			ret = cam_read(client, mc_ret_data, payload_len);
			if (ret != 0) {
				dev_err(&client->dev," %s(%d) MCU fw CMD ID Read Version Error - %d \n", __func__,
						__LINE__, ret);
				ret = -1;
				goto exit;
			}

			/* Verify CRC */
			orig_crc = mc_ret_data[payload_len - 2];
			calc_crc = errorcheck(&mc_ret_data[2], 32);
			if (orig_crc != calc_crc) {
				dev_err(&client->dev," %s(%d) MCU fw  CMD ID Version CRC ERROR 0x%02x != 0x%02x \n",
						__func__, __LINE__, orig_crc, calc_crc);
				ret = -1;
				goto exit;
			}

			/* Verify Errcode */
			errcode = mc_ret_data[payload_len - 1];
			if (errcode != ERRCODE_SUCCESS) {
				dev_err(&client->dev," %s(%d) MCU fw CMD ID Read Payload Error - 0x%02x \n", __func__,
						__LINE__, errcode);
				ret = -1;
				goto exit;
			}


			for (loop = 0 ; loop < VERSION_SIZE ; loop++)
				*(fw_version+loop) = mc_ret_data[2+loop];


			for(i = 0; i < VERSION_SIZE; i++)
			{
				if(bin_fw_version[i] != fw_version[i]) {
					dev_dbg(&client->dev,"Previous Firmware Version - (%c_%c_%.12s_%c_%c_%c_%c_%.7s)\n",
							fw_version[0], fw_version[1], &fw_version[2],
							fw_version[14], fw_version[15], fw_version[16], fw_version[17],&fw_version[18]);
					dev_dbg(&client->dev,"Current Firmware Version - (%c_%c_%.12s_%c_%c_%c_%c_%.7s)\n",
							bin_fw_version[0], bin_fw_version[1] , &bin_fw_version[2],
						       	bin_fw_version[14], bin_fw_version[15], bin_fw_version[16],
							bin_fw_version[17], &bin_fw_version[18]);
					ret = -1;
					goto exit;
				}
			}
			ret = ERRCODE_SUCCESS;
		}

	exit:
        /* unlock semaphore */
        mutex_unlock(&priv->cam_i2c_mutex);

        return ret;
}

unsigned short int cam_bload_calc_crc16(unsigned char *buf, int len)
{
        unsigned short int crc = 0;
        int i = 0;

        if (!buf || !(buf + len))
                return 0;

        for (i = 0; i < len; i++) {
                crc ^= buf[i];
        }

        return crc;
}

unsigned char cam_bload_inv_errorcheck(unsigned char *buf, int len)
{
        unsigned int checksum = 0x00;
        int i = 0;

        if (!buf || !(buf + len))
                return 0;

        for (i = 0; i < len; i++) {
                checksum = (checksum + buf[i]);
        }

        checksum &= (0xFF);
        return (~(checksum) + 1);
}



int cam_bload_parse_send_cmd(struct i2c_client *client,
                   unsigned char *bytearray, int rec_len, unsigned short int *orig_crc16)
{
        IHEX_RECORD *ihex_rec = NULL;
        unsigned char checksum = 0, calc_checksum = 0;
        int i = 0, ret = 0;

        if (!bytearray)
                return -1;

        ihex_rec = (IHEX_RECORD *) bytearray;
        ihex_rec->addr = htons(ihex_rec->addr);

        checksum = bytearray[rec_len - 1];

        calc_checksum = cam_bload_inv_errorcheck(bytearray, rec_len - 1);
        if (checksum != calc_checksum) {
                dev_err(&client->dev," Invalid Checksum 0x%02x != 0x%02x !! \n", checksum,
                       calc_checksum);
                return -1;
        }

        /*   TODO: send I2C Commands to Write */
        if ((ihex_rec->rectype == REC_TYPE_ELA) && (ihex_rec->addr == 0x0000) &&
            (ihex_rec->datasize = 0x02)) {
                /*   Upper 32-bit configuration */
                g_bload_flashaddr = (ihex_rec->recdata[0] <<
                                                        24) | (ihex_rec->
                                                               recdata[1]
                                                               << 16);

                dev_err(&client->dev,"Updated Flash Addr = 0x%08x \n", g_bload_flashaddr);

        } else if (ihex_rec->rectype == REC_TYPE_DATA) {
                /*   Flash Data into Flashaddr */

                g_bload_flashaddr =
                    (g_bload_flashaddr & 0xFFFF0000) | (ihex_rec->addr);
                *orig_crc16 ^=
                    cam_bload_calc_crc16(ihex_rec->recdata, ihex_rec->datasize);

                /*   Write Erase Pages CMD */
                g_bload_buf[0] = BL_WRITE_MEM_NS;
                g_bload_buf[1] = ~(BL_WRITE_MEM_NS);

                ret = cam_write(client, g_bload_buf, 2);
                if (ret < 0) {
                        dev_err(&client->dev,"Write Failed \n");
                        return -1;
                }

                /*   Wait for ACK or NACK */
                ret = cam_read(client, g_bload_buf, 1);
                if (ret < 0) {
                        dev_err(&client->dev,"Read Failed \n");
                        return -1;
                }

                if (g_bload_buf[0] != RESP_ACK) {
                        /*   NACK Received */
                        dev_err(&client->dev," NACK Received... exiting.. \n");
                        return -1;
                }

                g_bload_buf[0] = (g_bload_flashaddr & 0xFF000000) >> 24;
                g_bload_buf[1] = (g_bload_flashaddr & 0x00FF0000) >> 16;
                g_bload_buf[2] = (g_bload_flashaddr & 0x0000FF00) >> 8;
                g_bload_buf[3] = (g_bload_flashaddr & 0x000000FF);
                g_bload_buf[4] =
                    g_bload_buf[0] ^ g_bload_buf[1] ^ g_bload_buf[2] ^
                    g_bload_buf[3];

                ret = cam_write(client, g_bload_buf, 5);
                if (ret < 0) {
                        dev_err(&client->dev,"Write Failed \n");
                        return -1;
                }

                /*   Wait for ACK or NACK */
                ret = cam_read(client, g_bload_buf, 1);
                if (ret < 0) {
                        dev_err(&client->dev,"Read Failed \n");
                        return -1;
                }

                if (g_bload_buf[0] != RESP_ACK) {
                        /*   NACK Received */
                        dev_err(&client->dev," NACK Received... exiting.. \n");
                        return -1;
                }

                g_bload_buf[0] = ihex_rec->datasize - 1;
                checksum = g_bload_buf[0];
                for (i = 0; i < ihex_rec->datasize; i++) {
                        g_bload_buf[i + 1] = ihex_rec->recdata[i];
                        checksum ^= g_bload_buf[i + 1];
                }

                g_bload_buf[i + 1] = checksum;

                ret = cam_write(client, g_bload_buf, i + 2);
                if (ret < 0) {
                        dev_err(&client->dev,"Write Failed \n");
                        return -1;
                }

 poll_busy:
                /*   Wait for ACK or NACK */
                ret = cam_read(client, g_bload_buf, 1);
                if (ret < 0) {
                        dev_err(&client->dev,"Read Failed \n");
                        return -1;
                }

                if (g_bload_buf[0] == RESP_BUSY)
                        goto poll_busy;

                if (g_bload_buf[0] != RESP_ACK) {
                        /*   NACK Received */
                        dev_err(&client->dev," NACK Received... exiting.. \n");
                        return -1;
                }

        } else if (ihex_rec->rectype == REC_TYPE_SLA) {
                /*   Update Instruction pointer to this address */

        } else if (ihex_rec->rectype == REC_TYPE_EOF) {
                /*   End of File - Issue I2C Go Command */
                return 0;
        } else {

                /*   Unhandled Type */
                dev_err(&client->dev,"Unhandled Command Type \n");
                return -1;
        }

        return 0;
}

int cam_bload_go(struct i2c_client *client)
{
        int ret = 0;

        g_bload_buf[0] = BL_GO;
        g_bload_buf[1] = ~(BL_GO);

        ret = cam_write(client, g_bload_buf, 2);
        if (ret < 0) {
                dev_err(&client->dev,"Write Failed \n");
                return -1;
        }

        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Failed Read 1 \n");
                return -1;
        }

        /*   Start Address */
        g_bload_buf[0] = (FLASH_START_ADDRESS & 0xFF000000) >> 24;
        g_bload_buf[1] = (FLASH_START_ADDRESS & 0x00FF0000) >> 16;
        g_bload_buf[2] = (FLASH_START_ADDRESS & 0x0000FF00) >> 8;
        g_bload_buf[3] = (FLASH_START_ADDRESS & 0x000000FF);
        g_bload_buf[4] =
            g_bload_buf[0] ^ g_bload_buf[1] ^ g_bload_buf[2] ^ g_bload_buf[3];

        ret = cam_write(client, g_bload_buf, 5);
        if (ret < 0) {
                dev_err(&client->dev,"Write Failed \n");
                return -1;
        }

        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Failed Read 1 \n");
                return -1;
        }

        if (g_bload_buf[0] != RESP_ACK) {
                /*   NACK Received */
                dev_err(&client->dev," NACK Received... exiting.. \n");
                return -1;
        }

        return 0;
}

int cam_bload_read_fw(struct i2c_client *client,
			unsigned short int *orig_crc16)
{
        /* exclude NULL character at end of string */
	unsigned long hex_file_size = strlen(mcu_fw_buf) - 1;
        unsigned char wbuf[MAX_BUF_LEN];
        int i = 0, recindex = 0, ret = 0;

        for (i = 0; i < hex_file_size; i++) {
                if ((recindex == 0) && (mcu_fw_buf[i] == ':')) {
                } else if (mcu_fw_buf[i] == CR) {
				} else if (mcu_fw_buf[i] == '"' || mcu_fw_buf[i] =='\\' ||
						mcu_fw_buf[i] == 'n') {
                } else if (mcu_fw_buf[i] == LF) {
                        if (recindex == 0) {
                                break;
                        }

                        /*   Analyze Packet and Send Commands */
                        ret = cam_bload_parse_send_cmd(client, wbuf, recindex, orig_crc16);
                        if (ret < 0) {
                                dev_err(&client->dev,"Error in Processing Commands \n");
                                break;
                        }

                        recindex = 0;

                } else {
                        /*   Parse Rec Data */
                        if ((ret = cam_bload_ascii2hex(mcu_fw_buf[i])) < 0) {
                                dev_err(&client->dev,"Invalid Character - 0x%02x !! \n",
                                       mcu_fw_buf[i]);
                                break;
                        }

                        wbuf[recindex] = (0xF0 & (ret << 4));
                        i++;

                        if ((ret = cam_bload_ascii2hex(mcu_fw_buf[i])) < 0) {
                                dev_err(&client->dev,"Invalid Character - 0x%02x !!!! \n",
                                       mcu_fw_buf[i]);
                                break;
                        }

                        wbuf[recindex] |= (0x0F & ret);
                        recindex++;
                }
        }

        /* ------------ PROGRAM FLASH END ----------------------- */

        return ret;
}

int cam_bload_erase_flash(struct i2c_client *client)
{
        unsigned short int pagenum = 0x0000;
        int ret = 0, i = 0, checksum = 0;

        /* --------------- ERASE FLASH --------------------- */

        for (i = 0; i < NUM_ERASE_CYCLES; i++) {

                checksum = 0x00;
                /*   Write Erase Pages CMD */
                g_bload_buf[0] = BL_ERASE_MEM_NS;
                g_bload_buf[1] = ~(BL_ERASE_MEM_NS);

                ret = cam_write(client, g_bload_buf, 2);
                if (ret < 0) {
                        dev_err(&client->dev,"Write Failed \n");
                        return -1;
                }

                /*   Wait for ACK or NACK */
                ret = cam_read(client, g_bload_buf, 1);
                if (ret < 0) {
                        dev_err(&client->dev,"Read Failed \n");
                        return -1;
                }

                if (g_bload_buf[0] != RESP_ACK) {
                        /*   NACK Received */
                        dev_err(&client->dev," NACK Received... exiting.. \n");
                        return -1;
                }

                g_bload_buf[0] = (MAX_PAGES - 1) >> 8;
                g_bload_buf[1] = (MAX_PAGES - 1) & 0xFF;
                g_bload_buf[2] = g_bload_buf[0] ^ g_bload_buf[1];

                ret = cam_write(client, g_bload_buf, 3);
                if (ret < 0) {
                        dev_err(&client->dev,"Write Failed \n");
                        return -1;
                }

                /*   Wait for ACK or NACK */
                ret = cam_read(client, g_bload_buf, 1);
                if (ret < 0) {
                        dev_err(&client->dev,"Read Failed \n");
                        return -1;
                }

                if (g_bload_buf[0] != RESP_ACK) {
                        /*   NACK Received */
                        dev_err(&client->dev," NACK Received... exiting.. \n");
                        return -1;
                }

                for (pagenum = 0; pagenum < MAX_PAGES; pagenum++) {
                        g_bload_buf[(2 * pagenum)] =
                            (pagenum + (i * MAX_PAGES)) >> 8;
                        g_bload_buf[(2 * pagenum) + 1] =
                            (pagenum + (i * MAX_PAGES)) & 0xFF;
                        checksum =
                            checksum ^ g_bload_buf[(2 * pagenum)] ^
                            g_bload_buf[(2 * pagenum) + 1];
                }
                g_bload_buf[2 * MAX_PAGES] = checksum;

                ret = cam_write(client, g_bload_buf, (2 * MAX_PAGES) + 1);
                if (ret < 0) {
                        dev_err(&client->dev,"Write Failed \n");
                        return -1;
                }

 poll_busy:
                /*   Wait for ACK or NACK */
                ret = cam_read(client, g_bload_buf, 1);
                if (ret < 0) {
                        dev_err(&client->dev,"Read Failed \n");
                        return -1;
                }

                if (g_bload_buf[0] == RESP_BUSY)
                        goto poll_busy;

                if (g_bload_buf[0] != RESP_ACK) {
                        /*   NACK Received */
                        dev_err(&client->dev," NACK Received... exiting.. \n");
                        return -1;
                }

                dev_err(&client->dev," ERASE Sector %d success !! \n", i + 1);
        }

        /* ------------ ERASE FLASH END ----------------------- */

        return 0;
}

int cam_bload_read(struct i2c_client *client, unsigned int g_bload_flashaddr,
                   char *bytearray, unsigned int len)
{
        int ret = 0;

        g_bload_buf[0] = BL_READ_MEM;
        g_bload_buf[1] = ~(BL_READ_MEM);

        ret = cam_write(client, g_bload_buf, 2);
        if (ret < 0) {
                dev_err(&client->dev,"Write Failed \n");
                return -1;
        }

        /*   Wait for ACK or NACK */
        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Read Failed \n");
                return -1;
        }

        if (g_bload_buf[0] != RESP_ACK) {
                /*   NACK Received */
                dev_err(&client->dev," NACK Received... exiting.. \n");
                return -1;
        }

        g_bload_buf[0] = (g_bload_flashaddr & 0xFF000000) >> 24;
        g_bload_buf[1] = (g_bload_flashaddr & 0x00FF0000) >> 16;
        g_bload_buf[2] = (g_bload_flashaddr & 0x0000FF00) >> 8;
        g_bload_buf[3] = (g_bload_flashaddr & 0x000000FF);
        g_bload_buf[4] =
            g_bload_buf[0] ^ g_bload_buf[1] ^ g_bload_buf[2] ^ g_bload_buf[3];

        ret = cam_write(client, g_bload_buf, 5);
        if (ret < 0) {
                dev_err(&client->dev,"Write Failed \n");
                return -1;
        }

        /*   Wait for ACK or NACK */
        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Read Failed \n");
                return -1;
        }

        if (g_bload_buf[0] != RESP_ACK) {
                /*   NACK Received */
                dev_err(&client->dev," NACK Received... exiting.. \n");
                return -1;
        }

        g_bload_buf[0] = len - 1;
        g_bload_buf[1] = ~(len - 1);

        ret = cam_write(client, g_bload_buf, 2);
        if (ret < 0) {
                dev_err(&client->dev,"Write Failed \n");
                return -1;
        }

        /*   Wait for ACK or NACK */
        ret = cam_read(client, g_bload_buf, 1);
        if (ret < 0) {
                dev_err(&client->dev,"Read Failed \n");
                return -1;
        }

        if (g_bload_buf[0] != RESP_ACK) {
                dev_err(&client->dev," NACK Received... exiting.. \n");
                return -1;
        }

        ret = cam_read(client, bytearray, len);
        if (ret < 0) {
                dev_err(&client->dev,"Read Failed \n");
                return -1;
        }

        return 0;
}

int cam_bload_verify_flash(struct i2c_client *client,
                           unsigned short int orig_crc)
{
        char bytearray[FLASH_READ_LEN];
        unsigned short int calc_crc = 0;
        unsigned int flash_addr = FLASH_START_ADDRESS, i = 0;

        while ((i + FLASH_READ_LEN) <= FLASH_SIZE) {
                memset(bytearray, 0x0, FLASH_READ_LEN);

                if (cam_bload_read
                    (client, flash_addr + i, bytearray, FLASH_READ_LEN) < 0) {
                        dev_err(&client->dev," i2c_bload_read FAIL !! \n");
                        return -1;
                }

                calc_crc ^= cam_bload_calc_crc16(bytearray, FLASH_READ_LEN);
                i += FLASH_READ_LEN;
        }

        if ((FLASH_SIZE - i) > 0) {
                memset(bytearray, 0x0, FLASH_READ_LEN);

                if (cam_bload_read
                    (client, flash_addr + i, bytearray, (FLASH_SIZE - i))
                    < 0) {
                        dev_err(&client->dev," i2c_bload_read FAIL !! \n");
                        return -1;
                }

                calc_crc ^= cam_bload_calc_crc16(bytearray, FLASH_READ_LEN);
        }

        if (orig_crc != calc_crc) {
                dev_err(&client->dev," CRC verification fail !! 0x%04x != 0x%04x \n",
                       orig_crc, calc_crc);
                return -1;
        }

        dev_err(&client->dev," CRC Verification Success 0x%04x == 0x%04x \n", orig_crc,
               calc_crc);

        return 0;
}

static int cam_fw_update(struct i2c_client *client, unsigned char *cam_fw_version)
{
        int ret = 0;
	unsigned short int bload_crc16 = 0;

        ret = cam_bload_get_version(client);
        if (ret < 0) {
                dev_err(&client->dev," Error in Get Version \n");
                goto exit;
        }

	/* Erase firmware present in the MCU and flash new firmware*/
        ret = cam_bload_erase_flash(client);
        if (ret < 0) {
                dev_err(&client->dev," Error in Erase Flash \n");
                goto exit;
        }

        if (cam_bload_read_fw(client,&bload_crc16) < 0) {
                dev_err(&client->dev," verify_flash FAIL !! \n");
                goto exit;
        }

        if (cam_bload_verify_flash(client, bload_crc16) < 0) {
                dev_err(&client->dev," verify_flash FAIL !! \n");
                goto exit;
        }

	/* Reverting from bootloader mode */
        if (cam_bload_go(client) < 0) {
                dev_err(&client->dev," i2c_bload_go FAIL !! \n");
                goto exit;
        }
		dev_dbg(&client->dev,"(%s) - Firware Updated - (%c_%c_%.12s_%c_%c_%c_%c_%.7s)\n",
				__func__,cam_fw_version[0], cam_fw_version[1], &cam_fw_version[2], cam_fw_version[14], cam_fw_version[15], cam_fw_version[16], cam_fw_version[17], &cam_fw_version[18]);
 exit:
        return 0;
}

static int cam_board_setup(struct cam *priv)
{
    	struct camera_common_data *s_data = priv->s_data;
	struct device *dev = s_data->dev;
	int err = 0;

	err = camera_common_mclk_enable(s_data);
	if (err) {
	    	dev_err(dev, "Error %d turning on mclk\n",err);
		cam_power_off(s_data);
		return err;
	}
	return err;
}



static int cam_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device_node *node = client->dev.of_node;
	struct tegracam_device *tc_dev;
	struct cam *priv;
	int err;
	const struct of_device_id *match;

	dev_info(&client->dev, "Probing for sensor %s\n", id->name);

	if (!IS_ENABLED(CONFIG_OF) || !node)
		return -EINVAL;

	priv = devm_kzalloc(&client->dev,
			    sizeof(struct cam) + sizeof(struct v4l2_ctrl *) * NUM_CTRLS,  GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "unable to allocate memory!\n");
		return -ENOMEM;
	}

	tc_dev = devm_kzalloc(&client->dev,
			    sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev) {
		dev_err(&client->dev, "unable to allocate memory!\n");
		return -ENOMEM;
	}

	priv->i2c_client		= tc_dev->client = client;
	tc_dev->dev			= &client->dev;
	strncpy(tc_dev->name, id->name, sizeof(tc_dev->name));
	tc_dev->dev_regmap_config 	= &cam_regmap_config;
	tc_dev->sensor_ops 		= &cam_common_ops;
	tc_dev->v4l2sd_internal_ops 	= &cam_subdev_internal_ops;
	tc_dev->priv 			= priv;

	match = of_match_device(of_match_ptr(cam_of_match), &client->dev);
	if (match)
		priv->sensor_info = of_device_get_match_data(&client->dev);
	else
		priv->sensor_info = &econ_cam_sensor_info_tbl[id->driver_data];

	err = cam_core_initialize(tc_dev);
	if (err) {
		dev_err(&client->dev, "CAM core initialize failed\n");
		return err;
	}
        cam_ctrl_ops.numctrls = priv->sensor_info->numctrls;

	cam_ctrl_ops.ctrl_cid_list = priv->sensor_info->ctrl_cid_list;

	tc_dev->tcctrl_ops              = &cam_ctrl_ops;
	err = tegracam_device_register(tc_dev);
	if (err) {
		dev_err(&client->dev, "tegra camera driver registration failed\n");
		return err;
	}

	tc_dev->s_data->use_sensor_mode_id = priv->use_sensor_mode_id;

	dev_info(&client->dev, "%s : Use_sensor_mode_id = %d\n", __func__, tc_dev->s_data->use_sensor_mode_id);

	err = cam_power_on(tc_dev->s_data);
	if (err) {
		dev_err(&client->dev, "cam_power_on failed\n");
		return err;
	}

	priv->tc_dev 			= tc_dev;
	priv->s_data			= tc_dev->s_data;
	priv->subdev			= &tc_dev->s_data->subdev;
	tegracam_set_privdata(tc_dev, (void*)priv);

	err = cam_board_setup(priv);
	if (err) {
	    	dev_err(&client->dev, "board setup failed\n");
		return err;
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
	    	dev_err(&client->dev, "tegra camera subdev registration failed\n");
		return err;
	}


	dev_info(&client->dev, "Detected %s sensor\n", tc_dev->name);

	return 0;
}

static void
cam_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct cam *priv = (struct cam *)s_data->priv;

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);
	mutex_destroy(&priv->cam_i2c_mutex);
	/* Releasing the MCU firmware by the driver when rmmod is issued */
	if(is_fw_loaded == 1){

		release_firmware(mcu_fw);

		if( mcu_fw_buf != NULL )
		{
			pr_info("Freeing mcu_fw_buf");
			kfree(mcu_fw_buf);
		}
		is_fw_loaded = 0;
		pr_info("Firmware Released by the Driver");
	}
//	gpio_free(priv->reset_gpio);
//	gpio_free(priv->boot_gpio);
//	gpio_free(priv->flash_gpio);
}

static const struct i2c_device_id cam_id[] = {
	{ "eimx290", 0 },
	{ "ar0521", 1 },
	{ "ar2020", 2 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, cam_id);

static struct i2c_driver cam_i2c_driver = {
	.driver = {
		.name = "e-con_cam",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cam_of_match),
	},
	.probe = cam_probe,
	.remove = cam_remove,
	.id_table = cam_id,
};

module_i2c_driver(cam_i2c_driver);

MODULE_DESCRIPTION("Media Controller driver for e-con Cameras");
MODULE_AUTHOR("e-con Systems");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(AR2020_VERSION);
