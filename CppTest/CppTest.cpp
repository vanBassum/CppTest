
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




class Config;
class ConfigNode
{
    using ConfigValue = std::variant<std::monostate, int, float, std::string, std::string_view>;
    ConfigValue value;

    // Helper function to check if type T is one of the types in MyVariant
    template<typename T>
    void checkTypeInMyVariant() {
        static_assert(
            (std::is_same_v<T, std::variant_alternative_t<0, ConfigValue>> 
                || std::is_same_v<T, std::variant_alternative_t<1, ConfigValue>>     
                || std::is_same_v<T, std::variant_alternative_t<2, ConfigValue>>     
                || std::is_same_v<T, std::variant_alternative_t<3, ConfigValue>>
                || std::is_same_v<T, std::variant_alternative_t<4, ConfigValue>>
            ),"Provided type T is not one of the types in MyVariant");
    }
public:
    std::shared_ptr<ConfigNode> next;
    std::shared_ptr<ConfigNode> child;

    template<typename T>
    void Set(const T newValue) {
        checkTypeInMyVariant<T>();
        child = nullptr;
        next = nullptr;
        value = newValue;
    }
    
    template<typename T>
    bool Get(T& resultValue) {
        checkTypeInMyVariant<T>();
    
        if (!std::holds_alternative<T>(value)) {
            return false;
        }
    
        resultValue = std::get<T>(value);
        return true;
    }
    
    template<typename T>
    bool CheckType() {
        checkTypeInMyVariant<T>();
        return std::holds_alternative<T>(value);
    }

    virtual std::string GetKey() = 0;
    Config operator[](const std::string& key);
};


class RamNode : public ConfigNode
{
    std::string key;

public:
    RamNode(const std::string& key) : key(key) {}
    virtual std::string GetKey() override { return key; }
};





class YamlParser;
class YamlNode : public ConfigNode
{
    std::string_view key;
    friend YamlParser;

public:

    YamlNode(const std::string_view key) : key(key) {}
    virtual std::string GetKey() override { return std::string(key); }
};

class Config
{
    std::shared_ptr<ConfigNode> intern;
public:
    Config(const std::string& key)    {        intern = std::make_shared<RamNode>(key);    }
    Config(std::shared_ptr<ConfigNode> intern) : intern(intern) {}
    Config operator[](const std::string& key) { return (*intern)[key]; }
    std::string GetKey() { return intern->GetKey(); }


    template<typename T>    void Set(const T newValue) { intern->Set<T>(newValue); }
    template<typename T>    bool Get(T& resultValue) { return intern->Get<T>(resultValue); }
    template<typename T>    bool CheckType() { return intern->CheckType<T>(); }


    struct Visitor { 
        int depth = 0;
        int width = 0;
        virtual void Visit(Config& node) = 0; 
    };


    void VisitDepthFirst(Visitor& visitor)
    {
        visitor.Visit(*this);
        if (intern->child) {
            Config child(intern->child);
            visitor.depth++;
            child.VisitDepthFirst(visitor);
            visitor.depth--;
        }

        if (intern->next) {
            Config next(intern->next);
            visitor.width++;
            next.VisitDepthFirst(visitor);
            visitor.width--;
        }
    }
};


Config ConfigNode::operator [](const std::string& key) {
    std::shared_ptr<ConfigNode> iter = child;

    // Loop through the direct children to find an existing key.
    while (iter) {
        if (iter->GetKey() == key) {
            return Config(iter);
        }
        iter = iter->next;
    }

    // No key found, create a new ConfigNode for this key and add it to the children.
    auto newNode = std::make_shared<RamNode>(key);
    if (!child) {
        child = newNode;  // Initialize the child if it's not already initialized.
    }
    else {
        iter = child;
        while (iter->next) {
            iter = iter->next;
        }
        iter->next = newNode;  // Add the new node to the end of the list of children.
    }

    return Config(newNode);
}

class YamlParser
{
    // Advances idx to beginning of next line, or end of string.
    void AdvanceLine(const std::string_view& input, size_t& idx)
    {
        idx = input.find('\n', idx);
        if (idx == std::string::npos)
            idx = input.length();
        else
            idx++;
    }

    // Takes a single line, withoug advancing idx.
    bool getLine(const std::string_view& input, const size_t& idx, std::string_view& line)
    {
        size_t end = idx;
        AdvanceLine(input, end);
        if (end <= idx)
            return false;
        line = input.substr(idx, end - idx);
        return true;
    }

    // Checks if line contains valid key
    bool checkIfKey(const std::string_view& input, const size_t& idx) {
        std::string_view line;
        if (!getLine(input, idx, line))
            return false;

        size_t i = line.find(':');
        return i != std::string::npos;
    }

    // Returns number of indentations for current node
    size_t countIndents(const std::string_view& input, const size_t& idx)
    {
        std::string_view line;
        if (!getLine(input, idx, line))
            return std::string::npos;

        return line.find_first_not_of(" \t\n\r");
    }

    // Utility function to trim all whitespace characters from both ends of a string view
    std::string_view trimWhitespace(std::string_view value) {
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

    bool extractKey(const std::string_view& input, const size_t& idx, std::string_view& key)
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

    bool extractValue(const std::string_view& input, const size_t& idx, std::string_view& value)
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
        return true;
    }

    bool parseValue(const std::string_view& rawValue, std::shared_ptr<ConfigNode> node) {
        // Trim whitespace from the provided value
        std::string_view value = trimWhitespace(rawValue);
        
        if (value.length() == 0)
            return false;
        
        // Try to parse as int
        int parsedInt;
        auto intResult = std::from_chars(value.data(), value.data() + value.size(), parsedInt);
        if (intResult.ec == std::errc{} && intResult.ptr == value.data() + value.size()) {
            node->Set(parsedInt);
            return true;
        }
        
        // Try to parse as float
        float parsedFloat;
        auto floatResult = std::from_chars(value.data(), value.data() + value.size(), parsedFloat);
        if (floatResult.ec == std::errc{} && floatResult.ptr == value.data() + value.size()) {
            node->Set(parsedFloat);
            return true;
        }
        
        // If parsing attempts fail, set as string_view
        node->Set(value);
        return true;
    }

    bool ensureValidLine(const std::string_view& input, size_t& idx)
    {
        while (!checkIfKey(input, idx) && idx < input.length())
            AdvanceLine(input, idx);
        return idx < input.length();
    }

    std::shared_ptr<YamlNode> Parse(const std::string_view& yaml, size_t& idx)
    {
        // Find begin
        if (!ensureValidLine(yaml, idx))
            return nullptr;

        // IDX points to the beginning of the line, containing the key of this node.
        size_t indentations = countIndents(yaml, idx);
        if (indentations == std::string::npos)
            return nullptr;

        // Get the key
        std::string_view key;
        if (!extractKey(yaml, idx, key))
            return nullptr;   // Coulnt determine the key

        std::shared_ptr<YamlNode> node = std::make_shared<YamlNode>(key);
        //node->indentations = indentations;

        std::string_view value;
        if (extractValue(yaml, idx, value))
        {
            parseValue(value, node);    // TODO: What if this fails?
        }

        // Advance to next key
        AdvanceLine(yaml, idx);
        if (!ensureValidLine(yaml, idx))
            return node;

        // Check if this is a child
        size_t ind = countIndents(yaml, idx);
        if (ind > indentations)
        {
            node->child = Parse(yaml, idx); //TODO: Do we want to stop here if retuned nullptr?

            // Advance to next key
            if (!ensureValidLine(yaml, idx))
                return node;
        }

        // Check for next node
        ind = countIndents(yaml, idx);
        if (ind == indentations)
        {
            node->next = Parse(yaml, idx); //TODO: Do we want to stop here if retuned nullptr?
        }

        return node;
    }

    class YamlPrintVisitor : public Config::Visitor
    {

    public:

        void Visit(Config& node) override
        {
            std::string indentation(depth * 2, ' ');

            std::string_view svVal;
            std::string sVal;
            int iVal;
            float fVal;
            if (node.Get(sVal))
            {
                std::cout << indentation << node.GetKey() << ": " << sVal << " (string)" << std::endl;
            }                                                   
            else if (node.Get(svVal))                           
            {                                                   
                std::cout << indentation << node.GetKey() << ": " << svVal << " (string_view)" << std::endl;
            }                                                   
            else if (node.Get(iVal))                            
            {                                                   
                std::cout << indentation << node.GetKey() << ": " << iVal << " (int)" << std::endl;
            }                                                   
            else if (node.Get(fVal))                            
            {                                                   
                std::cout << indentation << node.GetKey() << ": " << fVal << " (float)" << std::endl;
            }                                                   
            else                                                
            {                                                   
                std::cout << indentation << node.GetKey() << ": " << std::endl;
            }

            std::cout << indentation << node.GetKey() << ": " << std::endl;
        }
    };

public:

    Config Parse(const std::string_view& yaml)
    {
        size_t idx = 0;
        std::shared_ptr<YamlNode> node = Parse(yaml, idx);
        if (node == nullptr)
            return Config("Root");  //TODO: Do we want to handle this differently?
        return Config(node);
    }

    void Print(Config& config)
    {
        YamlPrintVisitor printVisitor;
        config.VisitDepthFirst(printVisitor);
    }

};








const char* cfg = R"(
deviceTree:
  espGpio_0:
    compatible: EspGpio
  csLogic_0:
    compatible: CsLogic
  spiBus_0:
    compatible: espSpiBus
    host: HSPI_HOST
    dmaChannel: SPI_DMA_CH_AUTO
    mosi_io_num: GPIO_NUM_4
    miso_io_num: GPIO_NUM_35
    sclk_io_num: GPIO_NUM_33
    max_transfer_sz: 1024
  spiDevice_0:
    compatible: espSpiDevice
    spiBus: spiBus_0
    clock_speed_hz: 6000000
    spics_io_num: GPIO_NUM_NC
    queue_size: 7
    customCS: csLogic_0,0,4
  mcp23s17_0:
    compatible: mcp23s17
    spiDevice: spiDevice_0
  spiDevice_1:
    compatible: espSpiDevice
    spiBus: spiBus_0
    clock_speed_hz: 20000000
    spics_io_num: GPIO_NUM_NC
    queue_size: 7
    command_bits: 8
    customCS: csLogic_0,0,2
  max14830_0:
    compatible: max14830
    spiDevice: spiDevice_1
    isrPin: espGpio_0,4,7
  max14830_0_uart_0:
    compatible: max14830_uart
    maxDevice: max14830_0
    port: 0
  spiDevice_2:
    compatible: espSpiDevice
    spiBus: spiBus_0
    clock_speed_hz: 5000000
    spics_io_num: GPIO_NUM_NC
    queue_size: 7
    command_bits: 8
    customCS: csLogic_0,0,3
  pcf2123:
    compatible: pcf2123, IRtc
    spiDevice: spiDevice_2
  kc1Protocol_0:
    compatible: KC1Protocol, ICommandSource
    stream: max14830_0_uart_0
    rxSize: 64
    txSize: 64
  pinpadIO_0:
    compatible: PinpadIO
    outputRelais_1: max14830_0,0,0
    outputRelais_2: max14830_0,0,1
    outputRelais_3: max14830_0,0,2
    outputBuzzer: max14830_0,3,3
    inputDetect_1: max14830_0,1,0
    inputDetect_2: max14830_0,1,1
    inputDetect_3: max14830_0,1,2
    inputReset: max14830_0,1,3
  hd44780_0:
    compatible: hd44780
    mcpdevice: mcp23s17_0
  sntp_0:
    compatible: ESPNtp, INtp
    server: pool.ntp.org
  netIfDevice:
    compatible: netif
  lan87xx_0:
    compatible: lan87xx
    NetIF: netIfDevice
    dhcp_enable: 1
    static_ip: 172.16.10.10
    static_gw: 172.16.10.10
    static_dns: 172.16.10.10
  esp_wifi_0:
    compatible: esp_wifi
    NetIF: netIfDevice
    wifi_mode: WIFI_MODE_STA
    sta_ssid: default_ssid
    sta_password: password
    dhcp_enable: 1
    static_ip: 172.16.10.11
    static_gw: 172.16.10.11
    static_dns: 172.16.10.11

)";

void Test()
{
    YamlParser parser;
    //Create a config from a Yaml.
    Config config = parser.Parse(cfg);
    //Print the config as a Yaml to the console.
    parser.Print(config);
}


int main()
{
    Test();
    std::cout << "\n";
}

