#ifndef TMAGENT_TYPES_BACKENDTYPES_H
#define TMAGENT_TYPES_BACKENDTYPES_H

#include <QString>

namespace TmAgent {

/**
 * @brief BackendDescriptor 结构 - 后端插件元数据
 * 
 * 描述后端插件的基本信息，包括标识、版本、支持的模式等。
 */
struct BackendDescriptor {
    QString backendId;           // 后端唯一标识（kebab-case）
    QString displayName;         // 显示名称
    QString version;             // 后端版本（语义化版本）
    bool supportsDelegate;       // 是否支持委托模式
    bool supportsTeammate;       // 是否支持队友模式
    
    // SDK 版本兼容性
    int sdkVersionMajor = 1;     // SDK 主版本号
    int sdkVersionMinor = 0;     // SDK 次版本号
    
    // 默认构造函数
    BackendDescriptor()
        : supportsDelegate(false)
        , supportsTeammate(false)
        , sdkVersionMajor(1)
        , sdkVersionMinor(0)
    {}
    
    /**
     * @brief 验证元数据有效性
     * @return true 如果元数据有效
     */
    bool isValid() const {
        // 后端 ID 必须非空
        if (backendId.trimmed().isEmpty()) {
            return false;
        }
        
        // 至少支持一种模式（委托或队友）
        if (!supportsDelegate && !supportsTeammate) {
            return false;
        }
        
        return true;
    }
};

} // namespace TmAgent

#endif // TMAGENT_TYPES_BACKENDTYPES_H
