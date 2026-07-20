#pragma once
#include <string>
#include <map>

namespace SincroPro {

class HttpClient {
public:
    static std::string get(const std::string& url, const std::map<std::string, std::string>& headers = {});
    static std::string post(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {});
    static std::string put(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {});

private:
    static std::string request(const std::string& method, const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers);
};

} // namespace SincroPro
