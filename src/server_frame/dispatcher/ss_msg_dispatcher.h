//
// Created by owt50 on 2016/9/27.
//

#ifndef _DISPATCHER_SS_MSG_DISPATCHER_H
#define _DISPATCHER_SS_MSG_DISPATCHER_H

#pragma once

#include <design_pattern/singleton.h>
#include <config/compiler_features.h>

#include "dispatcher_implement.h"

namespace atbus {
    namespace protocol {
        class msg;
    }
}

class ss_msg_dispatcher : public dispatcher_implement, public util::design_pattern::singleton<ss_msg_dispatcher> {
public:
    typedef dispatcher_implement::msg_ptr_t msg_ptr_t;
    typedef dispatcher_implement::msg_type_t msg_type_t;

protected:
    ss_msg_dispatcher();

public:
    virtual ~ss_msg_dispatcher();

    virtual const char* name() const UTIL_CONFIG_OVERRIDE;
    virtual int32_t init() UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 数据解包
     * @param msg_container 填充目标
     * @param msg_buf 数据地址
     * @param msg_size 数据长度
     * @return 返回错误码或0
     */
    virtual int32_t unpack_msg(msg_ptr_t msg_container, const void* msg_buf, size_t msg_size) UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取任务信息
     * @param msg_container 填充目标
     * @return 相关的任务id
     */
    virtual uint64_t pick_msg_task(const msg_ptr_t msg_container) UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取消息名称
     * @param msg_container 填充目标
     * @return 消息名称
     */
    virtual const std::string& pick_msg_name(const msg_ptr_t msg_container) UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取消息名称
     * @param msg_container 填充目标
     * @return 消息类型ID
     */
    virtual msg_type_t pick_msg_type_id(const msg_ptr_t msg_container) UTIL_CONFIG_OVERRIDE;

    /**
     * @brief 获取消息名称到ID的映射
     * @param msg_name 消息名称
     * @return 消息类型ID
     */
    virtual msg_type_t msg_name_to_type_id(const std::string& msg_name) UTIL_CONFIG_OVERRIDE;

    /**
     * deal with cs message data
     * @param msg msg information
     * @param buffer data
     * @param len data length
     * @return 0 or error code
     */
    int32_t dispatch(const atbus::protocol::msg &msg, const void *buffer, size_t len);
public:
    int32_t send_to_proc(uint64_t bus_id, msg_ptr_t msg);
    int32_t send_to_proc(uint64_t bus_id, const void* msg_buf, size_t msg_len);
};


#endif //ATF4G_CO_SS_MSG_DISPATCHER_H
