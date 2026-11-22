// Native sale catalogue row statistics and GenericShop databinding construction.
// Included at the original definition site; declaration and initialization order are preserved.
    void SetStats(std::size_t index){
        if(!stats_visible_ || !Valid(scene_) || index>=rows_.size())return;
        const auto& row=rows_[index];
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseSpeedValue",row.speed);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseSpeedEquipmentValue",row.speed);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseSpeedCapacityValue",row.speed);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseAccValue",row.acceleration);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseAccEquipmentValue",row.acceleration);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseAccCapacityValue",row.acceleration);
        DATABINDING::_DATABINDING_WRITE_DATA_HASH_STRING_FROM_PARENT(scene_,"HorseHandling",row.handling);
    }
    bool Fill(int start,int requested,Any collection){
        const auto failed=[&](){binding_failed_=true;scene_filled_=false;return false;};
        if(!Owns() || !Valid(list_))return failed();
        if(rows_.empty())return true;
        DATABINDING::_DATABINDING_CLEAR_BINDING_ARRAY(static_cast<Hash>(list_));
        if(Valid(entries_))DATABINDING::_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(entries_));
        entries_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"ItemListEntries");
        if(!Valid(entries_))return failed();
        const int count=static_cast<int>(rows_.size());
        start=std::clamp(start,0,count-1);requested=std::clamp(requested,0,count);
        for(int slot=0;slot<std::min(count,requested+1);++slot){
            const int i=(start+slot)%count;const auto& row=rows_[static_cast<std::size_t>(i)];
            const auto name="tjc_row_"+std::to_string(i);
            const Any item=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(entries_),name.c_str());
            if(!Valid(item))return failed();
            // All fields are required: never authorize a partly bound quote.
            const std::array<Any,10> fields{
                DATABINDING::_DATABINDING_ADD_DATA_INT(item,"ItemIndex",i+1),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"uiItemID",name.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"uiItemType","TEXT"),
                DATABINDING::_DATABINDING_ADD_DATA_HASH(item,"uiItemGsui",HashOf("GSUI_TEXT_LIST_ITEM")),
                DATABINDING::_DATABINDING_ADD_DATA_HASH(item,"uiItemLabel",0),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"uiItemRawText",row.label.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_BOOL(item,"itemEnabled",true),
                DATABINDING::_DATABINDING_ADD_DATA_HASH(item,"rightlabel",0),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"rightRawText",row.detail.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_BOOL(item,"rightLabelVisible",!row.detail.empty())};
            if(!std::all_of(fields.begin(),fields.end(),Valid))return failed();
            DATABINDING::_DATABINDING_INSERT_UI_ITEM_TO_LIST_FROM_CONTEXT_HASH_ALIAS(list_,i,
                HashOf("GSUI_TEXT_LIST_ITEM"),item);
            if(collection && DATABINDING::_VIRTUAL_COLLECTION_EXISTS(collection))
                DATABINDING::_VIRTUAL_COLLECTION_ITEM_ADD(collection,i,HashOf("GSUI_TEXT_LIST_ITEM"),item);
            ++items_filled_;
        }
        return true;
    }
    bool BuildRoot(){
        if(root_ || !launched_ || !UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()))return false;
        if(Valid(DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop"))){
            last_error_="GenericShop root already exists";return false;
        }
        root_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER_FROM_PATH("","GenericShop");
        if(!Valid(root_)){last_error_="could not create GenericShop root";return false;}
        root_created_=true;
        token_=static_cast<int>(1U+(++serial_%0x7FFFFFFFU));
        owner_=DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"tjcHorseSaleOwner",token_);
        owner_claimed_=Valid(owner_) && DATABINDING::DATABINDING_READ_INT(owner_)==token_;
        const char* title=results_mode_&&HUD::DOES_TEXT_LABEL_EXIST("RESULTS")?"RESULTS":
            HUD::DOES_TEXT_LABEL_EXIST("HORSE_RACE")?"HORSE_RACE":
            HUD::DOES_TEXT_LABEL_EXIST("RACE")?"RACE":"GUNSMITH";
        const Any title_entry=DATABINDING::_DATABINDING_ADD_DATA_HASH(root_,"Title",
            title_hash_?title_hash_:HashOf(title));
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"DefaultCategoryIndex",0);
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"CategoryCount",1);
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"ItemListEntryIndex",0);
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"PageFilterCurrentPageIndex",0);
        // Optional inline native subtitle is sale-only; the original HASH
        // subtitle remains intact for every other user of the native scene.
        DATABINDING::_DATABINDING_ADD_DATA_HASH(root_,"PageFilterCurrentPageLabel",0);
        const auto subtitle=horse_market::subtitle::Text(horse_subtitle_);
        DATABINDING::_DATABINDING_ADD_DATA_STRING(root_,"TjcHorseSubtitleRaw",subtitle.c_str());
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"TjcSaleVisible",true);
        DATABINDING::_DATABINDING_ADD_DATA_STRING(root_,"TjcSaleSelectLabel","Sell");
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"TjcSaleSelectEnabled",true);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"TjcSaleSelectVisible",true);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"ShavingSplitterVisible",false);
        // ITEM_LIST_DESCRIPTION defaults to a purchase-price footer. Results
        // are not purchasable items; payout/profit belong in ItemInfo1 instead.
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"ItemPriceFooterVisible",true);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"uiPaletteVisible",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"InfoBoxVisible",false);
        list_=DATABINDING::_DATABINDING_ADD_UI_ITEM_LIST(root_,"ItemList");
        entries_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"ItemListEntries");
        const Any prompts=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"Prompts");
        scene_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"Scene");
        if(!Valid(title_entry) || !Valid(owner_) || !Valid(list_) || !Valid(entries_) || !Valid(scene_) || !Valid(prompts)){
            last_error_="could not create GenericShop fields";
            return false;
        }
        for(const auto& [name,label]:{std::pair{"Select","Sell"},std::pair{"Back","Back"}}){
            const bool select=std::string_view(name)=="Select";
            DATABINDING::_DATABINDING_ADD_DATA_STRING(prompts,(std::string(name)+"Label").c_str(),
                results_mode_&&!select?result_back_label_.c_str():label);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,(std::string(name)+"Enabled").c_str(),!select);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,(std::string(name)+"Visible").c_str(),!select);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,(std::string(name)+"Held").c_str(),false);
        }
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,"SelectHeldEnabled",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,"SelectHeldVisible",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"AltHorseStats",false);
        const Any price=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(scene_),"price_details");
        if(!Valid(price)){
            last_error_="could not create GenericShop price binding";
            return false;
        }
                // Native shop_butcher.c func_2201 lower purchase-price footer.
        DATABINDING::_DATABINDING_ADD_DATA_STRING(price,"purchaseLabel","TOTAL");
        DATABINDING::_DATABINDING_ADD_DATA_INT(price,"purchasePrice",sale_total_);
        // Match vanilla shop_horse_shop_sp.c price_details initialization.
        // modifiedPriceGold controls the discounted price only; omitting the
        // primary currency flag lets the footer also render cents as gold.
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"isGoldPrice",false);
        DATABINDING::_DATABINDING_ADD_DATA_INT(price,"tokenPrice",0);
        DATABINDING::_DATABINDING_ADD_DATA_STRING(price,"rightPriceRawText","");
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"locked",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"itemPriceRankLocked",false);
        DATABINDING::_DATABINDING_ADD_DATA_INT(price,"itemPriceRank",0);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"isAffordable",true);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"rightPriceTextVisible",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"modifiedPriceVisible",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"modifiedPriceGold",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"visible",true);
        for(const char* name:{"HorseSpeed","HorseAcc"}){
            for(const char* suffix:{"Value","EquipmentValue","CapacityValue"})
                DATABINDING::_DATABINDING_ADD_DATA_INT(scene_,(std::string(name)+suffix).c_str(),0);
            for(const char* suffix:{"MinValue","EquipmentMinValue","CapacityMinValue"})
                DATABINDING::_DATABINDING_ADD_DATA_INT(scene_,(std::string(name)+suffix).c_str(),0);
            for(const char* suffix:{"MaxValue","EquipmentMaxValue","CapacityMaxValue"})
                DATABINDING::_DATABINDING_ADD_DATA_INT(scene_,(std::string(name)+suffix).c_str(),10);
        }
        DATABINDING::_DATABINDING_ADD_DATA_HASH(scene_,"HorseHandling",0);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"StatsVisible",stats_visible_);
        DATABINDING::_DATABINDING_ADD_DATA_STRING(scene_,"ItemTooltip",tooltip_text_.c_str());
        if(!footer_text_.empty()){
            // The native ITEM_LIST_DESCRIPTION activity renders ItemInfo1 in
            // its description area above the bottom divider. Horse-stats
            // activity ignores this binding, so results use that activity
            // separately from the horse-stat betting picker.
            const Any info=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(scene_),"ItemInfo1");
            if(!Valid(info)){
                last_error_="could not create GenericShop footer";
                return false;
            }
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"FooterVisible",true);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(info,"Visible",true);
            DATABINDING::_DATABINDING_ADD_DATA_HASH(info,"Text",0);
            DATABINDING::_DATABINDING_ADD_DATA_STRING(info,"RawText",footer_text_.c_str());
            DATABINDING::_DATABINDING_ADD_DATA_HASH(info,"Style",static_cast<Hash>(432931142));
        }
        SetStats(0);
        if(!Fill(0,static_cast<int>(rows_.size()),0)){
            last_error_="could not bind complete sale rows";return false;
        }
        if(!Owns())last_error_="GenericShop owner not retained";
        return Owns();
    }
