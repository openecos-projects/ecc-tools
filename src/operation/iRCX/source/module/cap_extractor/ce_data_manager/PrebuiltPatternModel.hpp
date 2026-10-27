#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace prebuilt {
enum class Status { ok, geometry_fallback, domain_fallback };
struct Prediction { Status status=Status::ok; std::vector<double> caps; };

class Model {
  using Wire=std::pair<double,double>;
  struct Section {std::string master,lower,upper;double width=0;std::vector<Wire> left,right;};
  struct Etch {std::vector<double> widths,gaps,values;};
  struct Layer {std::string name;double width=0,wmin=0,smin=0,bottom=0,top=0;std::vector<Etch> etch;};
  struct Block {std::size_t input=0,output=0;bool tanh=false;std::vector<double> weight,bias;};
  struct Network {std::vector<double> xm,xs,ym,ys;std::vector<Block> blocks;};
  double window_=0;
  std::vector<Layer> layers_;
  std::vector<Network> networks_;

  static std::string word(std::istream& in) {
    std::string s;if(!(in>>s))throw std::runtime_error("truncated prebuilt model");return s;
  }
  static double number(std::istream& in) {
    double v=0;if(!(in>>v)||!std::isfinite(v))throw std::runtime_error("invalid model number");return v;
  }
  static std::size_t count(std::istream& in,std::size_t low,std::size_t high) {
    long long v=0;if(!(in>>v)||v<static_cast<long long>(low)||v>static_cast<long long>(high))
      throw std::runtime_error("invalid model dimension");
    return static_cast<std::size_t>(v);
  }
  static std::vector<double> numbers(std::istream& in,std::size_t size) {
    std::vector<double> v(size);for(auto& x:v)x=number(in);return v;
  }
  static void increasing(const std::vector<double>& v) {
    for(std::size_t i=1;i<v.size();++i)if(v[i]<=v[i-1])throw std::runtime_error("invalid etch axis");
  }
  std::size_t layerIndex(const std::string& name) const {
    for(std::size_t i=0;i<layers_.size();++i)if(layers_[i].name==name)return i;
    throw std::runtime_error("unknown layer in prebuilt model: "+name);
  }
  static bool decode(const std::string& key,Section& s) {
    std::istringstream in(key);double scale=0,width=0;int nl=0,nr=0;
    if(!(in>>s.master>>s.lower>>s.upper>>scale>>width>>nl>>nr)||!std::isfinite(scale)||scale<=0
       ||!std::isfinite(width)||width<=0||nl<0||nl>3||nr<0||nr>3)return false;
    s.width=width/scale;
    for(int side=0;side<2;++side){
      auto& wires=side==0?s.left:s.right;double edge=(side==0?-.5:.5)*s.width;
      const int n=side==0?nl:nr;
      for(int j=0;j<n;++j){
        double a=0,b=0;if(!(in>>a>>b)||!std::isfinite(a)||!std::isfinite(b))return false;
        a/=scale;b/=scale;
        if(a>=b||(side==0?b>=edge:a<=edge))return false;
        wires.emplace_back(a,b);edge=side==0?a:b;
      }
    }
    in>>std::ws;return in.eof();
  }
  bool inDomain(const Section& s,std::size_t m) const {
    if(s.lower!="SUBSTRATE"&&layerIndex(s.lower)>=m)throw std::runtime_error("invalid lower context");
    if(s.upper!="-"&&layerIndex(s.upper)<=m)throw std::runtime_error("invalid upper context");
    const auto& l=layers_[m];const double low=.5*std::min(l.width,l.wmin),high=std::max(8.,4*l.width),tol=1e-9;
    auto widthOK=[&](double w){return w>=low-tol&&w<=high+tol;};
    if(!widthOK(s.width))return false;
    for(int side=0;side<2;++side){
      double edge=s.width/2;
      for(const auto& [a,b]:(side==0?s.left:s.right)){
        const double near=side==0?-b:a,far=side==0?-a:b;
        if(!widthOK(b-a)||near-edge<.25*l.smin-tol||near-s.width/2>window_+tol)return false;
        edge=far;
      }
    }
    return true;
  }
  struct Bracket {std::size_t lo,hi;double t;};
  static Bracket bracket(const std::vector<double>& axis,double x) {
    const auto it=std::lower_bound(axis.begin(),axis.end(),x);
    const auto hi=it==axis.end()?axis.size()-1:static_cast<std::size_t>(it-axis.begin());
    const auto lo=(hi==0||x>=axis.back())?hi:hi-1;
    return {lo,hi,lo==hi?0.:(x-axis[lo])/(axis[hi]-axis[lo])};
  }
  static double shift(const Etch& e,double w,double g) {
    const auto a=bracket(e.widths,w),b=bracket(e.gaps,g);
    auto at=[&](std::size_t i,std::size_t j){return e.values[i*e.gaps.size()+j];};
    return (1-a.t)*((1-b.t)*at(a.lo,b.lo)+b.t*at(a.lo,b.hi))
         +a.t*((1-b.t)*at(a.hi,b.lo)+b.t*at(a.hi,b.hi));
  }
  static bool applyEtch(Section& s,const Layer& layer) {
    std::vector<Wire> wires{{-s.width/2,s.width/2}};
    wires.insert(wires.end(),s.left.begin(),s.left.end());wires.insert(wires.end(),s.right.begin(),s.right.end());
    std::vector<std::size_t> order(wires.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return wires[a].first<wires[b].first;});
    for(const auto& e:layer.etch){
      auto updated=wires;
      for(std::size_t k=0;k<order.size();++k){
        const auto i=order[k];const auto [a,b]=wires[i];
        const double left=k?a-wires[order[k-1]].second:std::numeric_limits<double>::infinity();
        const double right=k+1<order.size()?wires[order[k+1]].first-b:std::numeric_limits<double>::infinity();
        const double lo=a+shift(e,b-a,left),hi=b-shift(e,b-a,right);
        if(!std::isfinite(lo)||!std::isfinite(hi)||lo>=hi)return false;
        updated[i]={lo,hi};
      }
      for(std::size_t k=1;k<order.size();++k)
        if(updated[order[k-1]].second>=updated[order[k]].first)return false;
      wires=std::move(updated);
    }
    const auto [a,b]=wires[0];const double center=(a+b)/2;s.width=b-a;
    std::size_t i=1;
    for(auto* side:{&s.left,&s.right})for(auto& wire:*side){
      wire={wires[i].first-center,wires[i].second-center};++i;
    }
    return true;
  }
  std::vector<double> encode(const Section& s,std::size_t m) const {
    const auto& layer=layers_[m];std::vector<double> x;
    x.reserve(1+2*(layers_.size()+1)+24);x.push_back(std::log(s.width/layer.width));
    for(int side=0;side<2;++side){
      const auto& name=side==0?s.lower:s.upper;const std::string absent=side==0?"SUBSTRATE":"-";
      const auto context=name==absent?0:layerIndex(name)+1;
      for(std::size_t j=0;j<=layers_.size();++j)x.push_back(j==context?1.:0.);
    }
    for(int side=0;side<2;++side){
      const auto& wires=side==0?s.left:s.right;double edge=s.width/2;
      for(std::size_t j=0;j<3;++j){
        if(j>=wires.size()){x.insert(x.end(),4,0.);continue;}
        const auto [a,b]=wires[j];const double near=side==0?-b:a,far=side==0?-a:b;
        x.insert(x.end(),{1.,std::log((b-a)/layer.width),std::log1p((near-edge)/layer.smin),
                        std::log1p((near-s.width/2)/layer.smin)});edge=far;
      }
    }
    return x;
  }
  std::array<double,7> predict(const std::vector<double>& x,std::size_t m) const {
    const auto& net=networks_[m];std::array<double,1024> a{},b{};
    for(std::size_t i=0;i<x.size();++i)a[i]=(x[i]-net.xm[i])/net.xs[i];
    for(const auto& layer:net.blocks){
      for(std::size_t i=0;i<layer.output;++i){
        double v=layer.bias[i];const auto* row=layer.weight.data()+i*layer.input;
        for(std::size_t j=0;j<layer.input;++j)v+=row[j]*a[j];
        b[i]=layer.tanh?std::tanh(v):v;
      }
      a.swap(b);
    }
    std::array<double,7> result{};
    for(std::size_t i=0;i<7;++i){
      result[i]=std::exp(a[i]*net.ys[i]+net.ym[i]);
      if(!std::isfinite(result[i]))throw std::runtime_error("nonfinite prebuilt coefficient");
    }
    return result;
  }

 public:
  explicit Model(const std::string& path) {
    std::ifstream in(path);if(!in)throw std::runtime_error("cannot read prebuilt model");
    if(word(in)!="IRCX_PREBUILT_V1")throw std::runtime_error("invalid prebuilt model format");
    window_=number(in);if(window_<=0)throw std::runtime_error("invalid pattern window");
    const auto nl=count(in,1,128);
    for(std::size_t i=0;i<nl;++i){
      Layer l;l.name=word(in);l.width=number(in);l.wmin=number(in);l.smin=number(in);l.bottom=number(in);l.top=number(in);
      if(l.width<=0||l.wmin<=0||l.smin<=0||l.top<=l.bottom)throw std::runtime_error("invalid process geometry");
      for(const auto& previous:layers_)if(l.name==previous.name)throw std::runtime_error("duplicate process layer");
      const auto nt=count(in,0,32);
      for(std::size_t j=0;j<nt;++j){
        const auto nw=count(in,1,1024),ns=count(in,1,1024);Etch e;
        e.widths=numbers(in,nw);e.gaps=numbers(in,ns);e.values=numbers(in,nw*ns);
        increasing(e.widths);increasing(e.gaps);l.etch.push_back(std::move(e));
      }
      layers_.push_back(std::move(l));
    }
    if(count(in,1,128)!=nl)throw std::runtime_error("missing layer networks");
    networks_.resize(nl);std::vector<bool> seen(nl,false);
    for(std::size_t i=0;i<nl;++i){
      const auto m=layerIndex(word(in));if(seen[m])throw std::runtime_error("duplicate layer network");seen[m]=true;
      const auto ni=count(in,1,1024),no=count(in,1,1024),nb=count(in,1,8);
      if(ni!=1+2*(nl+1)+24||no!=7)throw std::runtime_error("wrong feature/output dimensions");
      auto& n=networks_[m];n.xm=numbers(in,ni);n.xs=numbers(in,ni);n.ym=numbers(in,no);n.ys=numbers(in,no);
      for(double v:n.xs)if(v<=0)throw std::runtime_error("invalid normalization scale");
      for(double v:n.ys)if(v<=0)throw std::runtime_error("invalid output scale");
      auto previous=ni;
      for(std::size_t j=0;j<nb;++j){
        Block b;b.input=count(in,1,1024);b.output=count(in,1,1024);b.tanh=count(in,0,1)!=0;
        if(b.input!=previous)throw std::runtime_error("incompatible network blocks");
        b.weight=numbers(in,b.input*b.output);b.bias=numbers(in,b.output);previous=b.output;n.blocks.push_back(std::move(b));
      }
      if(previous!=7)throw std::runtime_error("wrong final output dimensions");
    }
    if(word(in)!="END")throw std::runtime_error("missing model terminator");
    in>>std::ws;if(!in.eof())throw std::runtime_error("trailing model data");
  }

  Prediction query(const std::string& key) const {
    Section s;if(!decode(key,s))return {Status::geometry_fallback,{}};
    const auto m=layerIndex(s.master);
    if(!inDomain(s,m))return {Status::domain_fallback,{}};
    if(!applyEtch(s,layers_[m]))return {Status::geometry_fallback,{}};
    auto x=encode(s,m);const auto a=predict(x,m);
    for(std::size_t j=0;j<12;++j)std::swap(x[x.size()-24+j],x[x.size()-12+j]);
    const auto b=predict(x,m);
    Prediction result;result.caps.reserve(1+s.left.size()+s.right.size());
    result.caps.push_back(.5*(a[0]+b[0]));
    for(std::size_t j=0;j<s.left.size();++j)result.caps.push_back(.5*(a[1+j]+b[4+j]));
    for(std::size_t j=0;j<s.right.size();++j)result.caps.push_back(.5*(a[4+j]+b[1+j]));
    return result;
  }
};
} // namespace prebuilt
