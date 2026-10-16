#include "PopupPlacement.h"
#include <iostream>

int main()
{
    unsigned checks=0;
    const auto require=[&](bool value){++checks;if(!value){std::cerr<<"Popup placement check "<<checks<<" failed\n";std::exit(1);}};
    using w11widget::native::popupPlacement;
    for(const unsigned dpi:{96U,144U,192U}) {
        const auto result=popupPlacement({-70,1040,-10,1080},{-1920,0,0,1040},340,230,dpi);
        require(result&&result->right==0&&result->left==-static_cast<int>(340*dpi/96));
        require(result->bottom==1040-static_cast<int>(8*dpi/96)&&result->top>=0);
    }
    const auto top=popupPlacement({20,-40,90,0},{0,0,1000,900},340,230,96);
    require(top&&top->top==8&&top->left==20);
    const auto narrow=popupPlacement({20,80,90,110},{-100,-20,100,80},340,230,192);
    require(narrow&&narrow->left==-100&&narrow->top==-20&&narrow->right==100&&narrow->bottom==80);
    require(!popupPlacement({0,0,20,20},{0,0,100,100},0,230,96));
    require(!popupPlacement({0,0,20,20},{0,0,100,100},340,230,0));
    require(!popupPlacement({0,0,0,20},{0,0,100,100},340,230,96));
    require(!popupPlacement({0,0,20,20},{0,0,100,0},340,230,96));
    std::cout<<"Popup placement: "<<checks<<" checks PASS\n";
}
