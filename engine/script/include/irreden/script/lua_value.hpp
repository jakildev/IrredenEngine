#ifndef LUA_VALUE_H
#define LUA_VALUE_H

#include <irreden/script/ir_script_types.hpp>

#include <irreden/ir_profile.hpp>

#include <sol/sol.hpp>

#include <memory>
#include <string>
#include <map>

namespace IRScript {

struct ILuaValue {
    virtual ~ILuaValue() = default;

    virtual void parse(const sol::object &obj) = 0;
    virtual void reset_to_default() = 0;

    virtual ILuaValue &operator[](const std::string &key) {
        IR_ASSERT(false, "Not a table type");
        throw std::runtime_error("Not a table type");
    }
    virtual bool get_boolean() const {
        IR_ASSERT(false, "Not a boolean type");
        throw std::runtime_error("Not a boolean type");
    }
    virtual double get_number() const {
        IR_ASSERT(false, "Not a number type");
        throw std::runtime_error("Not a number type");
    }
    virtual int get_integer() const {
        IR_ASSERT(false, "Not an integer type");
        throw std::runtime_error("Not an integer type");
    }
    virtual std::string get_string() const {
        IR_ASSERT(false, "Not a string type");
        throw std::runtime_error("Not a string type");
    }

    virtual int get_enum() const {
        IR_ASSERT(false, "Not an enum type");
        throw std::runtime_error("Not an enum type");
    }
};

template <LuaType Type, typename EnumType = void> struct LuaValue;

template <> struct LuaValue<LuaType::BOOLEAN> : ILuaValue {
    bool value_;
    bool defaultValue_;

    LuaValue(bool defaultValue) : value_(defaultValue), defaultValue_(defaultValue) {}

    void parse(const sol::object &obj) override {
        IR_ASSERT(obj.is<bool>(), "Expected boolean");
        value_ = obj.as<bool>();
    }

    void reset_to_default() override {
        value_ = defaultValue_;
    }

    bool get_boolean() const override {
        return value_;
    }
};

template <> struct LuaValue<IRScript::LuaType::NUMBER> : ILuaValue {
    double value_;
    double defaultValue_;

    LuaValue(double defaultValue) : value_(defaultValue), defaultValue_(defaultValue) {}

    void parse(const sol::object &obj) override {
        IR_ASSERT(obj.is<double>(), "Expected number");
        value_ = obj.as<double>();
    }

    void reset_to_default() override {
        value_ = defaultValue_;
    }

    double get_number() const override {
        return value_;
    }
};

template <> struct LuaValue<LuaType::INTEGER> : ILuaValue {
    int value_;
    int defaultValue_;

    LuaValue(int defaultValue) : value_(defaultValue), defaultValue_(defaultValue) {}

    void parse(const sol::object &obj) override {
        IR_ASSERT(obj.is<int>(), "Expected integer");
        value_ = obj.as<int>();
    }

    void reset_to_default() override {
        value_ = defaultValue_;
    }

    int get_integer() const override {
        return value_;
    }
};

template <> struct LuaValue<LuaType::STRING> : ILuaValue {
    std::string value_;
    std::string defaultValue_;

    LuaValue(const std::string &defaultValue) : value_(defaultValue), defaultValue_(defaultValue) {}

    void parse(const sol::object &obj) override {
        IR_ASSERT(obj.is<std::string>(), "Expected string");
        value_ = obj.as<std::string>();
    }

    void reset_to_default() override {
        value_ = defaultValue_;
    }

    std::string get_string() const override {
        return value_;
    }
};

template <typename EnumType> struct LuaValue<LuaType::ENUM, EnumType> : ILuaValue {
    using EnumMappingFunction = std::function<EnumType(const std::string &)>;

    EnumType value_;
    EnumType defaultValue_;
    EnumMappingFunction stringToEnum_;

    LuaValue(EnumType defaultValue, EnumMappingFunction stringToEnum)
        : value_(defaultValue), defaultValue_(defaultValue), stringToEnum_(stringToEnum) {}

    void parse(const sol::object &obj) override {
        IR_ASSERT(obj.is<std::string>(), "Expected string for enum");
        value_ = stringToEnum_(obj.as<std::string>());
    }

    void reset_to_default() override {
        value_ = defaultValue_;
    }

    EnumType get_enum_value() const {
        return value_;
    }

    int get_enum() const override {
        return static_cast<int>(value_);
    }
};

template <> struct LuaValue<LuaType::TABLE> : ILuaValue {
    std::map<std::string, std::unique_ptr<ILuaValue>> value_;

    void parse(const sol::object &obj) override {
        IR_ASSERT(obj.is<sol::table>(), "Expected table");
        sol::table table = obj.as<sol::table>();
        for (auto &pair : value_) {
            const std::string &key = pair.first;
            sol::object luaValue = table[key];

            if (luaValue.valid()) {
                pair.second->parse(luaValue); // Parse the Lua value if valid
            } else {
                pair.second->reset_to_default(); // Use default if missing
            }
        }
    }

    void reset_to_default() override {
        for (auto &kv : value_) {
            kv.second->reset_to_default();
        }
    }

    ILuaValue &operator[](const std::string &key) override {
        auto it = value_.find(key);
        if (it != value_.end()) {
            return *(it->second);
        } else {
            IR_ASSERT(false, "Key not found");
            throw std::runtime_error("Key not found: " + key);
        }
    }
};

} // namespace IRScript

#endif /* LUA_VALUE_H */
