// SPDX-License-Identifier: GPL-2.0-or-later
// Sparse impulses and explicit, bounded band interpolation. Never global debounce.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
namespace surface_touch {
struct Cell { int index; double delta; };
inline bool identity(const std::string &s) {
    return s.size()==64 && s.find_first_not_of("0123456789abcdef")==std::string::npos;
}
struct Profile {
    std::string device; int vendor=0,product=0,rows=0,cols=0;
    std::vector<Cell> cells;
    std::vector<int> bandRows,bandColumns;
    std::vector<unsigned char> floor;
    static constexpr size_t MAX_TEXT=132096;
    bool model()const{return !floor.empty();}
    static bool bounded(const std::vector<int>&v,int size) {
        if(v.empty() || v.size()>size_t(std::min(4,size/10)))return false;
        int previous=-2,runs=0;
        for(int n:v){if(n<1 || n>=size-1 || n<=previous)return false;if(n!=previous+1)runs++;previous=n;}
        return runs<=2;
    }
    bool band()const{return !bandRows.empty() || !bandColumns.empty();}
    std::string encode() const {
        if(model()){
            static constexpr char hex[]="0123456789abcdef";
            std::ostringstream o;o<<"SPXNOISE3 "<<device<<' '<<vendor<<' '<<product<<' '<<rows<<' '<<cols<<' ';
            for(auto v:floor)o<<hex[v>>4]<<hex[v&15];
            return o.str()+"\n";
        }
        if(band()){
            const auto &v=bandRows.empty()?bandColumns:bandRows;
            std::ostringstream o;o<<"SPXNOISE2 "<<device<<' '<<vendor<<' '<<product<<' '<<rows<<' '<<cols<<' '<<(bandRows.empty()?'C':'R')<<' '<<v.size();
            for(int n:v)o<<' '<<n;
            return o.str()+"\n";
        }
        std::ostringstream o;o<<"SPXNOISE1 "<<device<<' '<<vendor<<' '<<product<<' '<<rows<<' '<<cols<<' '<<cells.size();
        for(auto c:cells)o<<' '<<c.index<<' '<<c.delta;
        return o.str()+"\n";
    }
    static Profile parse(const std::string &s,const std::string &device) {
        if(s.size()>MAX_TEXT || !identity(device))throw std::runtime_error("noise size/identity");
        Profile p;std::istringstream in(s);std::string tag,extra;int n;
        if(!(in>>tag>>p.device>>p.vendor>>p.product>>p.rows>>p.cols) || (tag!="SPXNOISE1"&&tag!="SPXNOISE2"&&tag!="SPXNOISE3") || p.device!=device ||
           p.vendor<1 || p.vendor>65535 || p.product<1 || p.product>65535 || p.rows<3 || p.cols<3 ||
           p.rows>256 || p.cols>256)throw std::runtime_error("noise header");
        if(tag=="SPXNOISE3"){
            std::string data;if(!(in>>data)||data.size()!=size_t(p.rows*p.cols*2)||in>>extra)throw std::runtime_error("noise map size");
            auto nibble=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("noise map encoding");};
            for(size_t i=0;i<data.size();i+=2)p.floor.push_back(static_cast<unsigned char>(nibble(data[i])*16+nibble(data[i+1])));
            if(std::none_of(p.floor.begin(),p.floor.end(),[](unsigned char v){return v!=0;}))throw std::runtime_error("empty noise map");
            return p;
        }
        if(tag=="SPXNOISE2"){
            char axis;if(!(in>>axis>>n) || (axis!='R'&&axis!='C') || n<1 || n>4)throw std::runtime_error("noise band header");
            auto &v=axis=='R'?p.bandRows:p.bandColumns;
            for(int i=0;i<n;i++){int index;if(!(in>>index))throw std::runtime_error("noise band index");v.push_back(index);}
            if(!bounded(v,axis=='R'?p.rows:p.cols) || in>>extra)throw std::runtime_error("noise band bounds");
            return p;
        }
        if(!(in>>n) || n<0 || n>std::min(16,p.rows*p.cols/100))throw std::runtime_error("noise cell count");
        for(int i=0;i<n;i++) {
            Cell c;if(!(in>>c.index>>c.delta) || c.index<0 || c.index>=p.rows*p.cols || !std::isfinite(c.delta) ||
                      c.delta<.08 || c.delta>.8 || (!p.cells.empty() && c.index<=p.cells.back().index))throw std::runtime_error("noise cell");
            p.cells.push_back(c);
        }
        if(in>>extra)throw std::runtime_error("noise trailing field");
        return p;
    }
};
class AdaptiveNoise {
    std::vector<std::array<int,3>> hits;
    std::vector<std::array<int,3>> rowHits,columnHits;
    std::array<int,3> abnormalWindows {};
    std::array<int,3> windowFrames {};
    std::vector<double> residual,observed;
public:
    static constexpr long DETECTION_MS=45000;
    static constexpr long SLIDE_MS=20000;
    bool sliding=false;
    // Accumulate only compatible, bounded evidence; never grow a blanket mask.
    static std::vector<unsigned char> asMap(const Profile &p){
        if(p.model())return p.floor;
        std::vector<unsigned char> v(p.rows*p.cols,0);
        for(auto c:p.cells)v[c.index]=static_cast<unsigned char>(std::ceil(std::min(1.,c.delta*2)*255));
        for(int r:p.bandRows)for(int c=0;c<p.cols;c++)v[r*p.cols+c]=255;
        for(int c:p.bandColumns)for(int r=0;r<p.rows;r++)v[r*p.cols+c]=255;
        return v;
    }
    static Profile combineMap(const Profile &active,const Profile &next){
        Profile p=next;p.cells.clear();p.bandRows.clear();p.bandColumns.clear();p.floor=asMap(active);auto n=asMap(next);
        // Retest learns residual amplitude after the active model, so accumulate it.
        for(size_t i=0;i<n.size();i++)p.floor[i]=static_cast<unsigned char>(std::min(255,int(p.floor[i])+int(n[i])));
        return Profile::parse(p.encode(),p.device);
    }
    static Profile refine(const Profile &active,const Profile &next) {
        if(active.device.empty())return Profile::parse(next.encode(),next.device);
        if(active.device!=next.device || active.vendor!=next.vendor || active.product!=next.product ||
           active.rows!=next.rows || active.cols!=next.cols)throw std::runtime_error("refine identity");
        Profile p=next;
        if(active.cells.empty()&&!active.band()&&!active.model())return Profile::parse(p.encode(),p.device);
        if(active.model()||next.model()||active.band()!=next.band())return combineMap(active,next);
        if(p.band()){
            if(active.bandRows.empty()!=p.bandRows.empty())return combineMap(active,next);
            auto &v=p.bandRows.empty()?p.bandColumns:p.bandRows;
            const auto &old=active.bandRows.empty()?active.bandColumns:active.bandRows;
            v.insert(v.end(),old.begin(),old.end());std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());
        }else{
            for(auto c:active.cells){auto it=std::find_if(p.cells.begin(),p.cells.end(),[&](Cell n){return n.index==c.index;});
                if(it==p.cells.end())p.cells.push_back(c);else it->delta=std::min(it->delta,c.delta);}
            std::sort(p.cells.begin(),p.cells.end(),[](Cell a,Cell b){return a.index<b.index;});
        }
        try{return Profile::parse(p.encode(),p.device);}catch(...){return combineMap(active,next);}
    }
    Profile result; bool learning=false,contaminated=false; int frames=0;
    int abnormalFrames=0,wideFrames=0,peakCells=0;
    std::vector<int> lineRows,lineColumns;
    long start=0; std::string state="idle";
    static double median(std::vector<double> v) {
        const auto n=v.size()/2;std::nth_element(v.begin(),v.begin()+n,v.end());return v[n];
    }
    template<class Image> static std::pair<double,int> neighborhood(const Image &im,int r,int c,double gate) {
        std::vector<double> v;
        for(int y=std::max(0,r-1);y<=std::min(int(im.rows())-1,r+1);y++)
            for(int x=std::max(0,c-1);x<=std::min(int(im.cols())-1,c+1);x++)if(y!=r || x!=c)v.push_back(im(y,x));
        const double m=median(v);int support=0;for(double n:v)if(n>m+gate/2)support++;
        return {m,support};
    }
    void begin(const std::string &id,int vendor,int product,int rows,int cols,long now,bool slide=false) {
        if(!identity(id) || rows<3 || cols<3 || rows>256 || cols>256)throw std::runtime_error("learn identity/shape");
        result={id,vendor,product,rows,cols,{},{},{},{}};hits.assign(rows*cols,{});residual.assign(rows*cols,0);observed.assign(rows*cols,0);
        rowHits.assign(rows,{});columnHits.assign(cols,{});
        sliding=slide;start=now;frames=0;abnormalFrames=wideFrames=peakCells=0;lineRows.clear();lineColumns.clear();
        abnormalWindows={};windowFrames={};
        contaminated=false;learning=true;state="learning";
    }
    // Once idle interference is observed, generate a reversible candidate from those
    // samples; intermittent/event-driven sensors need not fill three time buckets.
    void finish(long now) {
        if(!learning || now-start<(sliding?SLIDE_MS:DETECTION_MS))return;
        learning=false;
        if(frames==0){state="no_data";return;}
        auto present=[&](const std::array<int,3>&h){
            if(!sliding)return h[0]+h[1]+h[2]>0;
            for(int w=0;w<3;w++)if(h[w]<std::max(6,windowFrames[w]/5))return false;
            return true;
        };
        for(int r=0;r<result.rows;r++)if(present(rowHits[r]))lineRows.push_back(r);
        for(int c=0;c<result.cols;c++)if(present(columnHits[c]))lineColumns.push_back(c);
        if(lineColumns.empty()&&Profile::bounded(lineRows,result.rows))result.bandRows=lineRows;
        if(lineRows.empty()&&Profile::bounded(lineColumns,result.cols))result.bandColumns=lineColumns;
        if(result.band()){state="line_noise";return;}
        if(sliding){
            // Never learn the deliberate moving finger as an idle subtraction map.
            for(int n:windowFrames)if(n<3){state="no_data";return;}
            state=lineRows.empty()&&lineColumns.empty()?"quiet":"line_noise";return;
        }
        if(abnormalFrames==0){
            for(int n:windowFrames)if(n<3){state="no_data";return;}
            state="quiet";return;
        }
        if(wideFrames==0){
            for(int i=0;i<int(hits.size());i++)if(hits[i][0]+hits[i][1]+hits[i][2]>0)
                result.cells.push_back({i,std::clamp(residual[i]*.5,.08,.8)});
            if(!result.cells.empty()&&result.cells.size()<=std::min(size_t(16),hits.size()/100)){state="ready";return;}
        }
        result.cells.clear();result.floor.reserve(observed.size());
        for(double v:observed)result.floor.push_back(static_cast<unsigned char>(std::clamp(std::ceil(v*255),0.,255.)));
        state="ready";
    }
    std::string diagnostics() const {
        auto ids=[](const std::vector<int>&v){std::string s;for(size_t i=0;i<std::min(size_t(8),v.size());i++){if(i)s+=",";s+=std::to_string(v[i]);}return s.empty()?"none":s;};
        return "rows="+std::to_string(result.rows)+" cols="+std::to_string(result.cols)+
            " abnormal_frames="+std::to_string(abnormalFrames)+" wide_frames="+std::to_string(wideFrames)+
            " peak_cells="+std::to_string(peakCells)+"\nrow_ids="+ids(lineRows)+" column_ids="+ids(lineColumns)+
            " row_count="+std::to_string(lineRows.size())+" column_count="+std::to_string(lineColumns.size())+
            "\ncandidate_kind="+(result.model()?std::string("map"):result.band()?std::string("band"):state=="ready"?std::string("sparse"):std::string("none"))+"\n";
    }
    template<class Image> void observe(const Image &im,long now) {
        if(!learning)return;
        if(im.rows()!=result.rows || im.cols()!=result.cols){learning=false;state="inconclusive";return;}
        const int window=std::clamp(int((now-start)/((sliding?SLIDE_MS:DETECTION_MS)/3)),0,2);
        std::vector<double> positive,levels;positive.reserve(hits.size());levels.reserve(hits.size());
        for(int r=0;r<im.rows();r++)for(int c=0;c<im.cols();c++){
            double v=im(r,c);if(!std::isfinite(v) || v<0 || v>1){learning=false;state="inconclusive";return;}
            positive.push_back(std::max(0.,v-neighborhood(im,r,c,.08).first));
            levels.push_back(v);
        }
        const double med=median(positive);std::vector<double> deviations;
        for(double v:positive)deviations.push_back(std::abs(v-med));
        const double gate=std::max(.08,med+8*std::max(1./255.,median(deviations)));
        const double background=median(levels);
        std::vector<int> rowActive(result.rows),columnActive(result.cols);
        int wide=0,spikes=0;
        for(int r=0;r<im.rows();r++)for(int c=0;c<im.cols();c++){
            const int i=r*result.cols+c;const auto n=neighborhood(im,r,c,gate);
            const double excess=im(r,c)-std::min(background,n.first);
            if(excess>gate)observed[i]=std::max(observed[i],excess);
            if(im(r,c)-background>gate){rowActive[r]++;columnActive[c]++;}
            if(im(r,c)-n.first>gate){
                if(n.second<=1){hits[i][window]++;residual[i]=std::max(residual[i],im(r,c)-n.first);spikes++;}
                else wide++;
            }
        }
        frames++;windowFrames[window]++;
        int active=0;for(int n:rowActive)active+=n;peakCells=std::max(peakCells,active);
        if(wide || spikes || active){abnormalFrames++;abnormalWindows[window]++;}
        if(wide>1 || spikes>std::min(16,int(hits.size()/100))){contaminated=true;wideFrames++;}
        if(sliding){
            // Learn only narrow transverse excess, not the moving finger's footprint.
            // The wizard additionally requires motion and explicit single-finger confirmation.
            for(int r=1;r<result.rows-1;r++){
                int narrow=0;for(int c=0;c<result.cols;c++)
                    if(im(r,c)-std::max(im(r-1,c),im(r+1,c))>gate)narrow++;
                if(narrow>=std::max(6,result.cols/5))rowHits[r][window]++;
            }
            for(int c=1;c<result.cols-1;c++){
                int narrow=0;for(int r=0;r<result.rows;r++)
                    if(im(r,c)-std::max(im(r,c-1),im(r,c+1))>gate)narrow++;
                if(narrow>=std::max(6,result.rows/5))columnHits[c][window]++;
            }
        }else{
            for(int r=0;r<result.rows;r++)if(rowActive[r]>=std::max(6,result.cols/5))rowHits[r][window]++;
            for(int c=0;c<result.cols;c++)if(columnActive[c]>=std::max(6,result.rows/5))columnHits[c][window]++;
        }
        finish(now);
    }
    template<class Image> static int apply(const Profile &p,const std::string &device,int vendor,int product,Image &im) {
        if(p.device!=device || p.vendor!=vendor || p.product!=product || p.rows!=im.rows() || p.cols!=im.cols())return 0;
        std::vector<std::pair<int,double>> updates;
        if(p.model()){
            if(p.floor.size()!=size_t(p.rows*p.cols))return 0;
            for(int r=0;r<p.rows;r++)for(int c=0;c<p.cols;c++){
                const auto level=p.floor[r*p.cols+c];if(!level)continue;
                double value=std::max(0.,im(r,c)-double(level)/255.);
                // Reconstruct from original unmodelled neighbours where available;
                // no fixed screen exclusion region, onset delay or single-touch cap.
                for(int axis=0;axis<2;axis++){
                    int lo=(axis?r:c)-1,hi=(axis?r:c)+1,limit=axis?p.rows:p.cols;
                    auto masked=[&](int k){return p.floor[axis?k*p.cols+c:r*p.cols+k]!=0;};
                    auto sample=[&](int k){return axis?im(k,c):im(r,k);};
                    while(lo>=0&&masked(lo))lo--;
                    while(hi<limit&&masked(hi))hi++;
                    if(lo>=0&&hi<limit){double a=double((axis?r:c)-lo)/(hi-lo);value=std::max(value,(1-a)*sample(lo)+a*sample(hi));}
                    else if(lo>=0)value=std::max(value,sample(lo));
                    else if(hi<limit)value=std::max(value,sample(hi));
                }
                updates.push_back({r*p.cols+c,value});
            }
            for(auto u:updates)im(u.first/p.cols,u.first%p.cols)=u.second;
            return int(updates.size());
        }
        if(p.band()){
            if(!p.cells.empty() || (!p.bandRows.empty()&&!p.bandColumns.empty()))return 0;
            const bool row=!p.bandRows.empty();const auto &v=row?p.bandRows:p.bandColumns;
            if(!Profile::bounded(v,row?p.rows:p.cols))return 0;
            for(size_t i=0;i<v.size();){
                size_t end=i;while(end+1<v.size()&&v[end+1]==v[end]+1)end++;
                const int lower=v[i]-1,upper=v[end]+1;
                for(size_t k=i;k<=end;k++)for(int x=0;x<(row?p.cols:p.rows);x++){
                    const double a=double(v[k]-lower)/(upper-lower);
                    const double value=row?(1-a)*im(lower,x)+a*im(upper,x):(1-a)*im(x,lower)+a*im(x,upper);
                    updates.push_back({row?v[k]*p.cols+x:x*p.cols+v[k],value});
                }
                i=end+1;
            }
            // Boundary values are all original samples, not successively repaired neighbors.
            for(auto u:updates)im(u.first/p.cols,u.first%p.cols)=u.second;
            return int(updates.size());
        }
        for(auto c:p.cells){const int r=c.index/p.cols,x=c.index%p.cols;const auto n=neighborhood(im,r,x,c.delta);
            if(im(r,x)-n.first>c.delta && n.second<=1)updates.push_back({c.index,n.first});}
        // Read all neighbors before changing anything; genuine spatial support wins.
        for(auto u:updates)im(u.first/p.cols,u.first%p.cols)=u.second;
        return int(updates.size());
    }
};
} // namespace surface_touch
