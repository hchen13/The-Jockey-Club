// Admission and publication for genuine manual survey evidence. Included in
// scene_survey.cpp's private namespace after the collection helpers.
constexpr std::uint64_t kSurveyFileBytes=16ull*1024*1024,kSurveyTotalBytes=256ull*1024*1024;
constexpr unsigned kSurveyFiles=128;
bool SurveyName(const std::string& name) {
    return name.starts_with("TheJockeyClub-survey")||name.starts_with("TheJockeyClub-record-");
}
bool SurveyAdmission(const std::string& directory,std::uint64_t copies=1,std::uint64_t bytes=kSurveyFileBytes) {
    return bytes<=kSurveyFileBytes&&files::AdmitGenerated(directory,SurveyName,copies,bytes*copies,kSurveyFiles,kSurveyTotalBytes);
}
void WriteSurvey() {
    const int objects=g_job.objects,peds=g_job.peds,vehicles=g_job.vehicles;
    std::string issued;
    for(const auto& [model,o]:g_job.outcomes) {
        char b[200];std::snprintf(b,sizeof b,"# pin %s issued=%d granted=%d timeouts=%d",model.c_str(),o.issued,o.granted,o.timeouts);
        g_job.notes.emplace_back(b);issued+=(issued.empty()?"":", ")+model;
    }
    if(g_job.probe_map_entities)
        g_job.notes.emplace_back("# mapobjects: "+std::to_string(g_job.probed)+" map entities by model ("+std::to_string(kAllModelHashes.size())+
            " models from "+std::to_string(g_job.points.size())+" points, "+std::to_string(static_cast<int>(kProbeRadius))+" m, over "+
            std::to_string(g_job.probe_frames)+" frames)");
    else g_job.notes.emplace_back("# mapobjects: not probed (safe pool-only survey)");
    // The latest survey under the fixed name, and every survey under a
    // stamped one: two surveys in one session used to leave one file.
    const std::string stamp=Stamp();
    std::string text=Format(stamp,g_job.records);
    for(const auto& note:g_job.notes)text+=note+"\n";
    if(!SurveyAdmission(g_job.directory,2,text.size())){Log("survey capacity reached or path unsafe; existing surveys preserved");return;}
    const std::string path=g_job.directory+"\\TheJockeyClub-survey.txt";
    std::string file_stamp=stamp;for(auto& c:file_stamp)if(c==':' || c==' ')c='-';
    const auto archive=std::filesystem::path(g_job.directory)/("TheJockeyClub-survey-"+file_stamp+"-"+std::to_string(GetTickCount64())+".txt");
    if(!files::WriteNew(archive,text)||!files::ReplaceAtomically(path,text)){Log("could not publish survey; previous files preserved");return;}
    Log("wrote "+path+": "+std::to_string(g_job.probed)+" map entities by model over "+std::to_string(g_job.probe_frames)+" frames, "+
        std::to_string(g_job.pinned)+" pinned ("+std::to_string(g_job.pins)+" pins granted, "+
        std::to_string(g_job.timeouts)+" timed out, "+std::to_string(kModels.size())+" models from "+std::to_string(g_job.points.size())+
        " points over "+std::to_string(g_job.frames)+" frames), "+std::to_string(objects)+" objects, "+std::to_string(peds)+" peds, "+
        std::to_string(vehicles)+" vehicles within "+std::to_string(static_cast<int>(g_job.radius))+" m of "+
        std::to_string(g_job.player.x)+","+std::to_string(g_job.player.y)+","+std::to_string(g_job.player.z)+
        "; models that pinned: "+(issued.empty()?std::string("none"):issued));
}
