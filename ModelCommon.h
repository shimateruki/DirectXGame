#pragma once

class DirectXCommon;

// 3Dƒ‚ƒfƒ‹‚Ì‹¤’Ê•”•ª
class ModelCommon {
public:
    /// <summary>
    /// ‰Šú‰»
    /// </summary>
    /// <param name="dxCommon">DirectXŠî”Õ</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// DirectXŠî”Õ‚ğæ“¾
    /// </summary>
    /// <returns>DirectXŠî”Õ</returns>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    DirectXCommon* dxCommon_ = nullptr;
};