#pragma once

#include <string>
#include <map>
#include <windows.h>
#include <vector>
#include "InputNormalizer.h" // For WiiRemoteState button names

class ConfigManager {
public:
    static ConfigManager& GetInstance() {
        static ConfigManager instance;
        return instance;
    }

    void LoadActiveConfig(); // Loads from active_config.json
    void SaveActiveConfig(); // Saves to active_config.json (Auto-save)

    void LoadProfile(const std::string& profileName);   // Load from profiles/ dir
    void SaveProfile(const std::string& profileName);   // Save to profiles/ dir
    void SaveCurrentProfile();                          // Quick-save to active profile

    // File-dialog import/export: load/save to any absolute path.
    // LoadProfileFromPath copies the file into profiles/ so it appears in the
    // dropdown, then makes it the active profile.
    bool LoadProfileFromPath(const std::string& filePath);
    bool SaveProfileToPath(const std::string& filePath);

    bool GetMinimizeToTray() const { return m_minimizeToTray; }
    void SetMinimizeToTray(bool enable);

    std::vector<std::string> ListProfiles() const;
    void DeleteProfile(const std::string& profileName);
    std::string GetCurrentProfileName() const { return m_loadedProfileName; }

    const std::vector<WORD>& GetMappedKeys(const std::string& buttonName) const;
    void SetMappedKeys(const std::string& buttonName, const std::vector<WORD>& keys);

    // Provide access to the mappings for the GUI
    std::map<std::string, std::vector<WORD>>& GetMappings() { return m_mappings; }

    WORD StringToVK(const std::string& keyStr) const;
    std::string VKToString(WORD vk) const;

private:
    ConfigManager();
    ~ConfigManager() = default;

    bool m_minimizeToTray = true;
    std::map<std::string, std::vector<WORD>> m_mappings;
    std::string m_loadedProfileName = "default";
};
