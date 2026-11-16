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
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <set>

namespace captable {
enum class Status { ok, geometry_fallback, domain_fallback };
struct QueryResult { Status status=Status::ok; std::vector<double> caps; };

class Library {
  using Wire=std::pair<double,double>;
  struct Section {std::string master,lower,upper;double width=0;std::vector<Wire> left,right;};
  struct Etch {std::vector<double> widths,gaps,values;};
  struct Layer {std::string name;double width=0,wmin=0,smin=0,bottom=0,top=0;std::vector<Etch> etch;};
  static constexpr std::size_t max_features=19, max_caps=7, neighbors=64;
  struct Sample { std::array<double,max_features> x{}; std::array<double,max_caps> c{},logc{}; };
  using GroupKey=std::array<std::size_t,5>;
  double window_=0;
  std::vector<Layer> layers_;
  std::map<GroupKey,std::vector<Sample>> groups_;
  struct CacheShard {mutable std::shared_mutex mutex; std::unordered_map<std::string,QueryResult> values;};
  mutable std::array<CacheShard,64> cache_;
  static std::string word(std::istream& in) {
    std::string s;if(!(in>>s))throw std::runtime_error("truncated capacitance table");return s;
  }
  static double number(std::istream& in) {
    double v=0;if(!(in>>v)||!std::isfinite(v))throw std::runtime_error("invalid table number");return v;
  }
  static std::size_t count(std::istream& in,std::size_t low,std::size_t high) {
    long long v=0;if(!(in>>v)||v<static_cast<long long>(low)||v>static_cast<long long>(high))
      throw std::runtime_error("invalid table dimension");
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
    throw std::runtime_error("unknown layer in capacitance table: "+name);
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

  GroupKey groupKey(const Section& s,std::size_t m) const {
    return {m,s.lower=="SUBSTRATE"?0:layerIndex(s.lower)+1,
              s.upper=="-"?0:layerIndex(s.upper)+1,s.left.size(),s.right.size()};
  }
  static Section reflect(const Section& s) {
    auto r=s;r.left.clear();r.right.clear();
    for(auto [a,b]:s.right)r.left.emplace_back(-b,-a);
    for(auto [a,b]:s.left)r.right.emplace_back(-b,-a);
    return r;
  }
  static std::vector<double> reflectCaps(const std::vector<double>& c,std::size_t nl) {
    std::vector<double> r{c[0]};r.insert(r.end(),c.begin()+1+nl,c.end());
    r.insert(r.end(),c.begin()+1,c.begin()+1+nl);return r;
  }
  std::array<double,max_features> encode(const Section& s,std::size_t m) const {
    const auto& l=layers_[m];std::array<double,max_features> x{};
    std::size_t i=0;x[i++]=std::log(s.width/l.width);
    for(int side=0;side<2;++side){
      double edge=s.width/2;std::size_t rank=0;
      for(auto [a,b]:side==0?s.left:s.right){
        const double near=side==0?-b:a,far=side==0?-a:b;
        x[i++]=.5*std::log((b-a)/l.width);
        x[i++]=std::log1p((near-edge)/l.smin)/(rank+1);
        x[i++]=.5*std::log1p((near-s.width/2)/l.smin)/(rank+1);
        edge=far;++rank;
      }
    }
    for(double v:x)if(!std::isfinite(v))throw std::runtime_error("nonfinite table coordinate");
    return x;
  }
  void add(const Section& section,const std::vector<double>& c) {
    const auto m=layerIndex(section.master);
    if(!inDomain(section,m))throw std::runtime_error("table sample outside declared domain");
    auto s=section;if(!applyEtch(s,layers_[m]))throw std::runtime_error("invalid etched table sample");
    Sample sample;sample.x=encode(s,m);
    for(std::size_t j=0;j<c.size();++j)sample.c[j]=c[j];
    groups_[groupKey(s,m)].push_back(sample);
  }
  std::vector<double> interpolate(const std::vector<Sample>& samples,
                                  const std::array<double,max_features>& x,
                                  std::size_t nf,std::size_t nc) const {
    std::vector<std::pair<double,std::size_t>> distances;distances.reserve(samples.size());
    for(std::size_t i=0;i<samples.size();++i){
      double d=0;for(std::size_t j=0;j<nf;++j){const auto v=x[j]-samples[i].x[j];d+=v*v;}
      distances.emplace_back(d,i);
    }
    const auto k=std::min(neighbors,distances.size());
    std::partial_sort(distances.begin(),distances.begin()+k,distances.end());
    if(distances[0].first<1e-24){
      const auto& c=samples[distances[0].second].c;
      return {c.begin(),c.begin()+nc};
    }
    // Moving local affine interpolation of log coefficients. A fixed ridge
    // stabilizes sparse/collinear neighborhoods; no fitted weights are stored.
    constexpr auto nmax=max_features+1;
    std::array<std::array<double,nmax+max_caps>,nmax> a{};
    std::array<double,max_caps> lower,upper;
    lower.fill(std::numeric_limits<double>::infinity());upper.fill(-std::numeric_limits<double>::infinity());
    std::array<double,neighbors> w{};double sum=0;
    for(std::size_t i=0;i<k;++i){w[i]=std::pow(distances[0].first/distances[i].first,1.5);sum+=w[i];}
    const auto n=nf+1;
    for(std::size_t i=0;i<k;++i){
      const auto& s=samples[distances[i].second];std::array<double,nmax> row{};row[0]=1;
      for(std::size_t j=0;j<nf;++j)row[j+1]=s.x[j]-x[j];
      const auto weight=w[i]/sum;
      for(std::size_t j=0;j<n;++j){
        for(std::size_t q=0;q<n;++q)a[j][q]+=weight*row[j]*row[q];
        for(std::size_t q=0;q<nc;++q)a[j][n+q]+=weight*row[j]*s.logc[q];
      }
      for(std::size_t q=0;q<nc;++q){lower[q]=std::min(lower[q],s.logc[q]);upper[q]=std::max(upper[q],s.logc[q]);}
    }
    for(std::size_t j=0;j<n;++j)a[j][j]+=j?1e-4:1e-12;
    for(std::size_t col=0;col<n;++col){
      auto pivot=col;for(std::size_t j=col+1;j<n;++j)if(std::abs(a[j][col])>std::abs(a[pivot][col]))pivot=j;
      if(!std::isfinite(a[pivot][col])||std::abs(a[pivot][col])<1e-20)throw std::runtime_error("singular table interpolation");
      std::swap(a[col],a[pivot]);const auto v=a[col][col];
      for(std::size_t q=col;q<n+nc;++q)a[col][q]/=v;
      for(std::size_t j=0;j<n;++j)if(j!=col){const auto factor=a[j][col];for(std::size_t q=col;q<n+nc;++q)a[j][q]-=factor*a[col][q];}
    }
    std::vector<double> result(nc);
    for(std::size_t q=0;q<nc;++q){
      result[q]=std::exp(std::clamp(a[0][n+q],lower[q],upper[q]));
      if(!std::isfinite(result[q]))throw std::runtime_error("nonfinite interpolated capacitance");
    }
    return result;
  }
  QueryResult evaluate(const std::string& key) const {
    Section s;if(!decode(key,s))return {Status::geometry_fallback,{}};
    const auto m=layerIndex(s.master);
    if(!inDomain(s,m))return {Status::domain_fallback,{}};
    if(!applyEtch(s,layers_[m]))return {Status::geometry_fallback,{}};
    auto x=encode(s,m),xr=encode(reflect(s),m);
    // One canonical orientation gives exact reflection symmetry, including ties.
    const bool flip=s.left.size()>s.right.size() || (s.left.size()==s.right.size() && xr<x);
    if(flip){s=reflect(s);x=xr;}
    const auto it=groups_.find(groupKey(s,m));
    if(it==groups_.end())return {Status::domain_fallback,{}};
    auto caps=interpolate(it->second,x,1+3*(s.left.size()+s.right.size()),1+s.left.size()+s.right.size());
    if(flip)caps=reflectCaps(caps,s.left.size());
    return {Status::ok,std::move(caps)};
  }
 public:
  explicit Library(const std::string& path) {
    std::ifstream in(path);if(!in)throw std::runtime_error("cannot read capacitance table");
    if(word(in)!="IRCX_CAP_TABLE_V1")throw std::runtime_error("invalid capacitance table format");
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
    const auto rows=count(in,1,10000000);std::string line;std::getline(in,line);
    std::set<std::string> keys;
    for(std::size_t i=0;i<rows;++i){
      if(!std::getline(in,line)||line.size()>10000)throw std::runtime_error("truncated or invalid table row");
      const auto tab=line.find('\t');if(tab==std::string::npos)throw std::runtime_error("table row needs tab separator");
      const auto key=line.substr(0,tab);Section s;
      if(!decode(key,s)||!keys.insert(key).second)throw std::runtime_error("invalid or duplicate table key");
      std::istringstream values(line.substr(tab+1));auto c=numbers(values,1+s.left.size()+s.right.size());
      values>>std::ws;if(!values.eof())throw std::runtime_error("trailing table coefficients");
      for(double v:c)if(v<0)throw std::runtime_error("negative table coefficient");
      add(s,c);add(reflect(s),reflectCaps(c,s.left.size()));
    }
    if(word(in)!="END")throw std::runtime_error("missing table terminator");
    in>>std::ws;if(!in.eof())throw std::runtime_error("trailing table data");
    for(auto& [key,samples]:groups_){
      std::sort(samples.begin(),samples.end(),[](const auto&a,const auto&b){return a.x<b.x;});
      std::vector<Sample> unique;unique.reserve(samples.size());
      for(std::size_t i=0;i<samples.size();){
        auto s=samples[i];auto end=i+1;
        while(end<samples.size()&&samples[end].x==s.x){for(std::size_t j=0;j<max_caps;++j)s.c[j]+=samples[end].c[j];++end;}
        for(std::size_t j=0;j<max_caps;++j){s.c[j]/=end-i;s.logc[j]=std::log(std::max(s.c[j],1e-18));}
        unique.push_back(s);i=end;
      }
      samples=std::move(unique);
    }
  }
  QueryResult query(const std::string& key) const {
    auto& shard=cache_[std::hash<std::string>{}(key)%cache_.size()];
    {std::shared_lock lock(shard.mutex);auto it=shard.values.find(key);if(it!=shard.values.end())return it->second;}
    auto result=evaluate(key);
    {std::unique_lock lock(shard.mutex);if(shard.values.size()<4096)shard.values.emplace(key,result);}
    return result;
  }
};
} // namespace captable
