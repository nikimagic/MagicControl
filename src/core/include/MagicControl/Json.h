#pragma once
// Small strict JSON reader for bounded service/release/manifest documents. It does not
// interpret strings as code or flatten the GitHub asset list with regexes.
#include <charconv>
#include <cstring>
#include <string_view>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace mc::json {
struct Json {
    using Object=std::map<std::string,Json>;
    using Array=std::vector<Json>;
    std::variant<std::nullptr_t,bool,double,std::string,Array,Object> value;
    const Json& at(const std::string& key) const {
        static const Json empty{nullptr};
        const auto* obj=std::get_if<Object>(&value);
        if(!obj)return empty;
        const auto it=obj->find(key);return it==obj->end()?empty:it->second;
    }
    std::string text() const {const auto* v=std::get_if<std::string>(&value);return v?*v:std::string{};}
    bool flag() const {const auto* v=std::get_if<bool>(&value);return v&&*v;}
    double number() const {const auto* v=std::get_if<double>(&value);return v?*v:-1;}
    const Array& array() const {static const Array empty;const auto* v=std::get_if<Array>(&value);return v?*v:empty;}
};
class JsonReader {
    std::string_view input; std::size_t pos{};
    explicit JsonReader(std::string_view s):input(s){}
    [[noreturn]] static void fail(){throw std::runtime_error("Некорректный JSON");}
    void ws(){while(pos<input.size()&&(input[pos]==' '||input[pos]=='\r'||input[pos]=='\n'||input[pos]=='\t'))++pos;}
    char take(){if(pos==input.size())fail();return input[pos++];}
    bool eat(char c){ws();if(pos<input.size()&&input[pos]==c){++pos;return true;}return false;}
    unsigned hex(){unsigned n=0;for(int i=0;i<4;++i){char c=take();n<<=4;if(c>='0'&&c<='9')n+=c-'0';else if(c>='A'&&c<='F')n+=c-'A'+10;else if(c>='a'&&c<='f')n+=c-'a'+10;else fail();}return n;}
    static void utf8(std::string& s,unsigned c){
        if(c<128)s+=static_cast<char>(c);
        else if(c<2048){s+=static_cast<char>(192|(c>>6));s+=static_cast<char>(128|(c&63));}
        else if(c<65536){s+=static_cast<char>(224|(c>>12));s+=static_cast<char>(128|((c>>6)&63));s+=static_cast<char>(128|(c&63));}
        else {s+=static_cast<char>(240|(c>>18));s+=static_cast<char>(128|((c>>12)&63));s+=static_cast<char>(128|((c>>6)&63));s+=static_cast<char>(128|(c&63));}
    }
    std::string string(){
        ws();if(take()!='"')fail();std::string out;
        for(;;){unsigned char c=take();if(c=='"')break;if(c<32)fail();if(c!='\\'){out+=static_cast<char>(c);continue;}
            switch(take()){
                case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;
                case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
                case 'u':{unsigned u=hex();if(u>=0xD800&&u<=0xDBFF){if(take()!='\\'||take()!='u')fail();unsigned lo=hex();if(lo<0xDC00||lo>0xDFFF)fail();u=0x10000+((u-0xD800)<<10)+lo-0xDC00;}else if(u>=0xDC00&&u<=0xDFFF)fail();utf8(out,u);break;}
                default:fail();
            }
        }return out;
    }
    Json node(int depth){
        if(depth>40)fail();
        ws();if(pos==input.size())fail();char c=input[pos];
        if(c=='"')return {string()};
        if(eat('{')){Json::Object o;if(eat('}'))return {o};do{auto k=string();if(!eat(':'))fail();if(!o.emplace(k,node(depth+1)).second)fail();}while(eat(','));if(!eat('}'))fail();return {o};}
        if(eat('[')){Json::Array a;if(eat(']'))return {a};do{if(a.size()>10000)fail();a.push_back(node(depth+1));}while(eat(','));if(!eat(']'))fail();return {a};}
        for(auto [word,value]:{std::pair{"true",Json{true}},std::pair{"false",Json{false}},std::pair{"null",Json{nullptr}}}){
            if(input.substr(pos,strlen(word))==word){pos+=strlen(word);return value;}
        }
        const auto start=pos;if(input[pos]=='-')++pos;
        if(pos==input.size()||input[pos]<'0'||input[pos]>'9')fail();
        if(input[pos]=='0')++pos;else while(pos<input.size()&&input[pos]>='0'&&input[pos]<='9')++pos;
        if(pos<input.size()&&input[pos]=='.'){++pos;const auto first=pos;while(pos<input.size()&&input[pos]>='0'&&input[pos]<='9')++pos;if(pos==first)fail();}
        if(pos<input.size()&&(input[pos]=='e'||input[pos]=='E')){++pos;if(pos<input.size()&&(input[pos]=='+'||input[pos]=='-'))++pos;const auto first=pos;while(pos<input.size()&&input[pos]>='0'&&input[pos]<='9')++pos;if(pos==first)fail();}
        double number{};auto result=std::from_chars(input.data()+start,input.data()+pos,number);if(result.ec!=std::errc{})fail();return {number};
    }
public:
    static Json parse(std::string_view s){JsonReader r{s};auto v=r.node(0);r.ws();if(r.pos!=s.size())fail();return v;}
};
}
