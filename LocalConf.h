#ifndef LOCAL_CONF_H
#define LOCAL_CONF_H

#include <string>
#include <vector>
#include <stdint.h>
#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/string.h>

std::string getConfigPath();

class LocalConf {
public:
    int loadConf();
    int saveConf();
    int initNewConf();
    LocalConf(std::string path)
    : configPath(path), 
        maxThreadNum(16),
        localPort(52025),
        rdmaGidIndex(3),
        defaultRate(100.0),
        blockSize(1024), //in kbytes
        blockNum(256)
    {
        this->savedFolderPath = wxStandardPaths::Get().GetDocumentsDir();
    }
    ~LocalConf(){
        saveConf();
    }

    int getMaxThreadNum() const { return maxThreadNum; }
    int getLocalPort() const { return localPort; }
    int getRdmaGidIndex() const { return rdmaGidIndex; }
    double getDefaultRate() const { return defaultRate; }
    int getBlockSize() const { return blockSize; }
    int getBlockNum() const { return blockNum; }
    wxString getSavedFolderPath() const { return savedFolderPath; }
    void setSavedFolderPath(const wxString& path) { savedFolderPath = path; }

private:
    std::string configPath;

    //for conn listen
    int maxThreadNum;
    int localPort;

    //for rdma nic
    int rdmaGidIndex;
    double defaultRate;

    //for memory
    int blockSize;
    int blockNum;

    //for file save
    wxString savedFolderPath;

    bool isCommentOrEmpty(const std::string& line) const;
    std::string& trim(std::string& str);
    int splitComma(const std::string& splitString, std::vector<std::string>& splitArray);

    // 安全转换函数
    bool safeStringToInt(const std::string& str, int& result, const std::string& fieldName);
    bool safeStringToULongLong(const std::string& str, unsigned long long& result, const std::string& fieldName);
    bool safeStringToDouble(const std::string& str, double& result, const std::string& fieldName);
    int createDefaultConf();
};

#endif