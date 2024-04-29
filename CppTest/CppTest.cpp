
#include <windows.h>
#include <psapi.h>

#include <vector>
#include <string>
#include <string_view>
#include <iostream>
#include <memory>
#include <sstream>
#include <algorithm>
#include <charconv>
#include <variant>
#include <cctype>
#include <charconv>

#define ESP_LOGE(tag, format, ...) printf("%s: " format "\n", tag, ##__VA_ARGS__)



enum Result
{
    Ok,
    Error
};

class ConfigNode
{
    constexpr static const char* TAG = "ConfigNode";
public:
    virtual std::shared_ptr<ConfigNode> GetNext() = 0;
    virtual std::shared_ptr<ConfigNode> GetChild() = 0;
    virtual std::string_view GetKey() = 0;

    virtual Result Set(const int value)  { ESP_LOGE(TAG, "Not supported"); return Error; }
    virtual Result Get(int& value) { ESP_LOGE(TAG, "Not supported"); return Error; }

    virtual Result Set(const float value) { ESP_LOGE(TAG, "Not supported"); return Error; }
    virtual Result Get(float& value) { ESP_LOGE(TAG, "Not supported"); return Error; }

    virtual Result Set(const std::string value) { ESP_LOGE(TAG, "Not supported"); return Error; }
    virtual Result Get(std::string& value) { ESP_LOGE(TAG, "Not supported"); return Error; }
};




class YamlParser
{
public:

    // Advances idx to beginning of next line, or end of string.
    static void AdvanceLine(const std::string_view& input, size_t& idx)
    {
        idx = input.find('\n', idx);
        if (idx == std::string::npos)
            idx = input.length();
        else
            idx++;
    }

    // Takes a single line, withoug advancing idx.
    static bool getLine(const std::string_view& input, const size_t& idx, std::string_view& line)
    {
        size_t end = idx;
        AdvanceLine(input, end);
        if (end <= idx)
            return false;
        line = input.substr(idx, end - idx);
        return true;
    }

    // Checks if line contains valid key
    static bool checkIfKey(const std::string_view& input, const size_t& idx) {
        std::string_view line;
        if (!getLine(input, idx, line))
            return false;

        size_t i = line.find(':');
        return i != std::string::npos;
    }

    // Returns number of indentations for current node
    static size_t countIndents(const std::string_view& input, const size_t& idx)
    {
        std::string_view line;
        if (!getLine(input, idx, line))
            return std::string::npos;

        return line.find_first_not_of(" \t\n\r");
    }

    // Utility function to trim all whitespace characters from both ends of a string view
    static std::string_view trimWhitespace(std::string_view value) {
        auto isNotWhitespace = [](char ch) {
            return !std::isspace(static_cast<unsigned char>(ch));
            };

        // Find the first non-whitespace character from the start
        auto start = std::find_if(value.begin(), value.end(), isNotWhitespace);

        // Find the last non-whitespace character from the end
        auto end = std::find_if(value.rbegin(), value.rend(), isNotWhitespace).base();

        // Return a trimmed view from start to end
        return (start < end) ? std::string_view(&*start, end - start) : std::string_view();
    }

    static bool extractKey(const std::string_view& input, const size_t& idx, std::string_view& key)
    {
        std::string_view line;
        if (!getLine(input, idx, line))
            return false;

        size_t seperator = line.find(':');
        size_t start = line.find_first_not_of(" \t\n\r");

        if (seperator == std::string::npos)
            return false;

        if (start == std::string::npos)
            return false;

        if (start >= seperator)
            return false;

        key = line.substr(start, seperator - start);
        return true;
    }

    static bool extractValue(const std::string_view& input, const size_t& idx, std::string_view& value)
    {
        std::string_view line;
        if (!getLine(input, idx, line))
            return false;

        size_t seperator = line.find(':');

        if (seperator == std::string::npos)
            return false;
        else
            seperator++; // Consume :

        if (seperator >= line.length())
            return false;

        value = line.substr(seperator, line.length() - seperator - 1); //Consume \n

        // Trim leading and trailing whitespaces from the value
        size_t start = value.find_first_not_of(" \t\n\r");
        size_t end = value.find_last_not_of(" \t\n\r");

        if (start == std::string::npos || end == std::string::npos)
            return false;
        
        value = value.substr(start, end - start + 1);

        if (value.empty())
            return false;

        return true;
    }

    static bool ensureValidLine(const std::string_view& input, size_t& idx)
    {
        while (!checkIfKey(input, idx) && idx < input.length())
            AdvanceLine(input, idx);
        return idx < input.length();
    }

    static size_t findBegin(const std::string_view& input, const size_t index)
    {
        size_t idx = index;
        while (!checkIfKey(input, idx) && idx < input.length())
            AdvanceLine(input, idx);
        return idx < input.length() ? idx : std::string::npos;
    }
};

class YamlNode : public ConfigNode
{
    const char* yaml;       // Points to the start of the yaml
    const size_t index;     // Points to the linestart of this node in yaml (or std::string::npos if begin not found)
public:

    YamlNode(const char* yaml, const size_t index = 0) : yaml(yaml), index(YamlParser::findBegin(yaml, index))
    {
    }

    virtual std::string_view GetKey() override
    {
        std::string_view key;
        if (YamlParser::extractKey(yaml, index, key))
            return key;
        return "";
    }

    virtual Result Get(std::string& value) override
    {
        std::string_view val;
        if (!YamlParser::extractValue(yaml, index, val))
            return Result::Error;

        value = val;
        return Result::Ok;
    }

    virtual Result Get(float& value) override
    {
        std::string_view val;
        if (!YamlParser::extractValue(yaml, index, val))
            return Result::Error;

        char* endPtr = nullptr;
        value = std::strtof(val.data(), &endPtr);

        // Check if the conversion was successful and the entire string was consumed
        if (endPtr == val.data() + val.size())
            return Result::Ok;

        return Result::Error;
    }

    virtual Result Get(int& value) override
    {
        std::string_view val;
        if (!YamlParser::extractValue(yaml, index, val))
            return Result::Error;

        char* endPtr = nullptr;
        value = std::strtol(val.data(), &endPtr, 10);

        // Check if the conversion was successful and the entire string was consumed
        if (endPtr == val.data() + val.size())
            return Result::Ok;

        return Result::Error;
    }


    virtual std::shared_ptr<ConfigNode> GetNext() override 
    {
        size_t idx = index;
        size_t indentations = YamlParser::countIndents(yaml, idx);
        if (indentations == std::string::npos)
            return nullptr;

        // Keep advancing lines, untill we find a node with the same indentations
        size_t yamlLength = strlen(yaml);
        while (idx < yamlLength)
        {
            // Advance to next key
            YamlParser::AdvanceLine(yaml, idx);
            if (!YamlParser::ensureValidLine(yaml, idx))
                return nullptr;

            // Get intentation
            size_t indents = YamlParser::countIndents(yaml, idx);
            if (indents == std::string::npos)
                return nullptr;

            // End of this node
            if (indents < indentations)
                return nullptr; 

            // Next node found
            if (indents == indentations)
                return std::make_shared<YamlNode>(yaml, idx);
        }
        return nullptr;
    }

    virtual std::shared_ptr<ConfigNode> GetChild() override 
    {
        size_t idx = index;
        size_t indentations = YamlParser::countIndents(yaml, idx);
        if (indentations == std::string::npos)
            return nullptr;

        // Keep advancing lines, untill we find a node with the same indentations
        size_t yamlLength = strlen(yaml);
        while (idx < yamlLength)
        {
            // Advance to next key
            YamlParser::AdvanceLine(yaml, idx);
            if (!YamlParser::ensureValidLine(yaml, idx))
                return nullptr;

            // Get intentation
            size_t indents = YamlParser::countIndents(yaml, idx);
            if (indents == std::string::npos)
                return nullptr;

            // End of this node
            if (indents < indentations)
                return nullptr;

            // Child node found
            if (indents > indentations)
                return std::make_shared<YamlNode>(yaml, idx);

            return nullptr;
        }
        return nullptr;
    }

};





const char* cfg = R"(
DeviceTree:
    MyFirstDevice:
        Compatible: MAXUART
        Baud: 115200
        MaxVoltage: 5.7
    SecondDevice:
        Compatible: Display
        Uart: MyFirstDevice
)";


void Print(std::shared_ptr<ConfigNode> node, int depth = 0)
{
    std::string indentation(depth * 2, ' ');

    std::string sValue;
    int iValue;
    float fValue;

    if (node->Get(iValue) == Result::Ok)
        std::cout << indentation << node->GetKey() << ": i " << iValue << std::endl;
    else if(node->Get(fValue) == Result::Ok)
        std::cout << indentation << node->GetKey() << ": f " << fValue << std::endl;
    else if (node->Get(sValue) == Result::Ok)
        std::cout << indentation << node->GetKey() << ": s " << sValue << std::endl;
    else
        std::cout << indentation << node->GetKey() << ": " << std::endl;

    std::shared_ptr<ConfigNode> temp = node->GetChild();
    if(temp)
        Print(temp, depth + 1);

    temp = node->GetNext();
    if (temp)
        Print(temp, depth);
}



int main()
{

    std::shared_ptr<ConfigNode> node = std::make_shared<YamlNode>(cfg);

    Print(node);


    std::cout << "\n";
}

