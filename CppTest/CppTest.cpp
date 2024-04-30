
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
    virtual std::string_view GetKey() const = 0;

    virtual std::shared_ptr<ConfigNode> GetNext() const = 0;
    virtual std::shared_ptr<ConfigNode> GetChild() const = 0;

    virtual Result SetNext (std::shared_ptr<ConfigNode> node)  { ESP_LOGE(TAG, "Not supported"); return Error; }
    virtual Result SetChild (std::shared_ptr<ConfigNode> node) { ESP_LOGE(TAG, "Not supported"); return Error; }

    virtual Result Set(const int value)  { ESP_LOGE(TAG, "Not supported"); return Error; }
    virtual Result Get(int& value) const { ESP_LOGE(TAG, "Not supported"); return Error; }

    virtual Result Set(const float value) { ESP_LOGE(TAG, "Not supported"); return Error; }
    virtual Result Get(float& value) const { ESP_LOGE(TAG, "Not supported"); return Error; }

    virtual Result Set(const std::string& value) { ESP_LOGE(TAG, "Not supported"); return Error; }
    virtual Result Get(std::string& value) const { ESP_LOGE(TAG, "Not supported"); return Error; }    
};

struct IRamNodeValue
{
    enum class Types
    {
        NODE,
        FLOAT,
        INT,
        STRING
    };
    virtual Types GetType() = 0;
};

struct RamNodeValue_Node : public IRamNodeValue
{
    std::shared_ptr<ConfigNode> child  = nullptr;
    std::shared_ptr<ConfigNode> next = nullptr;

    RamNodeValue_Node(std::shared_ptr<ConfigNode> child, std::shared_ptr<ConfigNode> next) : child(child), next(next)
    {
    }

    virtual Types GetType() override { return Types::NODE; }
};

struct RamNodeValue_float : public IRamNodeValue
{
    float value = 0.0f;
    RamNodeValue_float(const float value) : value(value)
    {
    }

    virtual Types GetType() override { return Types::FLOAT; }
};

struct RamNodeValue_int : public IRamNodeValue
{
    int value = 0;
    RamNodeValue_int(const int value) : value(value)
    {
    }
    virtual Types GetType() override { return Types::INT; }
};

struct RamNodeValue_string : public IRamNodeValue
{
    std::string value;
    RamNodeValue_string(const std::string& value) : value(value)
    {
    }
    virtual Types GetType() override { return Types::STRING; }
};

class RamNode : public ConfigNode
{
    std::string key;
    std::shared_ptr<IRamNodeValue> value = nullptr;

    bool IsType(IRamNodeValue::Types type) const
    {
        if (value == nullptr)
            return false;

        return value->GetType() == type;
    }

public:

    RamNode(const std::string& key) : key(key)    {    }

    virtual std::string_view GetKey() const override { return key; }

    virtual std::shared_ptr<ConfigNode> GetNext() const override
    { 
        if (!IsType(IRamNodeValue::Types::NODE))
            return nullptr;
        return std::static_pointer_cast<RamNodeValue_Node>(value)->next;
    }
   
    virtual std::shared_ptr<ConfigNode> GetChild() const override
    {
        if (!IsType(IRamNodeValue::Types::NODE))
            return nullptr;
        return std::static_pointer_cast<RamNodeValue_Node>(value)->child;
    }

    virtual Result SetNext(std::shared_ptr<ConfigNode> node)  override
    { 
        if (!IsType(IRamNodeValue::Types::NODE))
            value = std::make_shared<RamNodeValue_Node>(nullptr, nullptr);
    
        std::static_pointer_cast<RamNodeValue_Node>(value)->next = node;
        return Result::Ok;
    }
    
    virtual Result SetChild(std::shared_ptr<ConfigNode> node) override
    { 
        if (!IsType(IRamNodeValue::Types::NODE))
            value = std::make_shared<RamNodeValue_Node>(nullptr, nullptr);
    
        std::static_pointer_cast<RamNodeValue_Node>(value)->child = node;
        return Result::Ok;
    }

    virtual Result Get(int& val) const override
    { 
        if (!IsType(IRamNodeValue::Types::INT))
            return Result::Error;
        val = std::static_pointer_cast<RamNodeValue_int>(value)->value;
        return Result::Ok;
    }

    virtual Result Get(float& val) const override
    {
        if (!IsType(IRamNodeValue::Types::FLOAT))
            return Result::Error;
        val = std::static_pointer_cast<RamNodeValue_float>(value)->value;
        return Result::Ok;
    }

    virtual Result Get(std::string& val) const override
    {
        if (!IsType(IRamNodeValue::Types::STRING))
            return Result::Error;
        val = std::static_pointer_cast<RamNodeValue_string>(value)->value;
        return Result::Ok;
    }

    virtual Result Set(const int val) override 
    { 
        value = std::make_shared<RamNodeValue_int>(val);
        return Result::Ok;
    }

    virtual Result Set(const float val) override
    {
        value = std::make_shared<RamNodeValue_float>(val);
        return Result::Ok;
    }

    virtual Result Set(const std::string& val) override
    {
        value = std::make_shared<RamNodeValue_string>(val);
        return Result::Ok;
    }

    //virtual Result Set(std::shared_ptr<ConfigNode> node) override
    //{
    //    value = std::make_shared<RamNodeValue_Node>(node->GetChild(), node->GetNext());
    //    key = node->GetKey();
    //    return Result::Ok;
    //}
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

    virtual std::string_view GetKey() const override
    {
        std::string_view key;
        if (YamlParser::extractKey(yaml, index, key))
            return key;
        return "";
    }

    virtual Result Get(std::string& value) const override
    {
        std::string_view val;
        if (!YamlParser::extractValue(yaml, index, val))
            return Result::Error;

        value = val;
        return Result::Ok;
    }

    virtual Result Get(float& value) const override
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

    virtual Result Get(int& value) const override
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

    virtual std::shared_ptr<ConfigNode> GetNext() const override
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

    virtual std::shared_ptr<ConfigNode> GetChild() const override
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


class Config;
class IConfigVisitor
{
public:
    virtual void Visit(Config& config, int depth) = 0;
};

class Config
{
    std::shared_ptr<ConfigNode> internalNode;

    Config(std::shared_ptr<ConfigNode> internalNode) : internalNode(internalNode)
    {
    }

public:

    Config(const std::string& key)
    {
        internalNode = std::make_shared<RamNode>(key);
    }

    static Config FromYaml(const char* yaml)
    {
        std::shared_ptr<ConfigNode> internalNode = std::make_shared<YamlNode>(yaml);
        return Config(internalNode);
    }

    Config operator[](const std::string& key) 
    { 
        std::shared_ptr<ConfigNode> iterator = internalNode->GetChild();
        
        if (iterator == nullptr)
        {
            std::shared_ptr<ConfigNode> factory = std::make_shared<RamNode>(key);
            internalNode->SetChild(factory);
            return Config(factory);
        }

        std::shared_ptr<ConfigNode> last;
        while (iterator)
        {
            if (key == iterator->GetKey())
                return Config(iterator);
            last = iterator;
            iterator = iterator->GetNext();
        }

        last->SetNext(std::make_shared<RamNode>(key));
        return Config(last);
    }

    std::string_view GetKey() const                     { return internalNode->GetKey(); }
    template<typename T> Result Get(T& value) const     { return internalNode->Get(value); }

    template<typename T> Result Set(const T& value)      { return internalNode->Set(value); }

    // template<> Result Set<Config>(const Config& value) { return internalNode->Set(value.internalNode); }


    Result AddChildNode(const Config& value)
    {
        std::shared_ptr<ConfigNode> iterator = internalNode->GetChild();

        if (iterator == nullptr)
            return internalNode->SetChild(value.internalNode);  

        std::shared_ptr<ConfigNode> last;
        while (iterator)
        {
            last = iterator;
            iterator = iterator->GetNext();
        }

        return last->SetNext(value.internalNode);
    }



    void DepthFirstSearch(IConfigVisitor& visitor, int depth = 0)
    {
        visitor.Visit(*this, depth);
        auto childPtr = internalNode->GetChild();
        if (childPtr)
        {
            Config child(childPtr);
            child.DepthFirstSearch(visitor, depth + 1);
        }

        auto nextPtr = internalNode->GetNext();

        while (nextPtr)
        {
            Config next(nextPtr);
            next.DepthFirstSearch(visitor, depth);
            nextPtr = nextPtr->GetNext();
        }
    }

    // Result GetNext(Config& result) const                
    // { 
    //     auto next = internalNode->GetNext();
    //     if (!next)
    //         return Result::Error;
    //     result = Config(next);
    //     return Result::Ok;
    // }
    // 
    // 
    // Result GetChild(Config& result) const               { return Config(internalNode->GetChild()); }
};


class ConfigPrinter : public IConfigVisitor
{
public:

    void Visit(Config& node, int depth) override
    {
        std::string indentation(depth * 2, ' ');
        std::string sValue;
        int iValue;
        float fValue;

        if (node.Get(iValue) == Result::Ok)
            std::cout << indentation << node.GetKey() << ": i " << iValue << std::endl;
        else if (node.Get(fValue) == Result::Ok)
            std::cout << indentation << node.GetKey() << ": f " << fValue << std::endl;
        else if (node.Get(sValue) == Result::Ok)
            std::cout << indentation << node.GetKey() << ": s " << sValue << std::endl;
        else
            std::cout << indentation << node.GetKey() << ": " << std::endl;
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


void Print(Config& node, int depth = 0)
{
    ConfigPrinter printer;
    node.DepthFirstSearch(printer);
}



int main()
{
    Config root("Root");

    root["testing"]["MyValue"].Set(5);

    root.AddChildNode(Config::FromYaml(cfg));

    Print(root);
    std::cout << "\n";
}

