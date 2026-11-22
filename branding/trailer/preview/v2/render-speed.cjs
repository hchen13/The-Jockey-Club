// node render-speed.cjs <original videos directory> <work directory> <output directory> <ffmpeg>
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { spawnSync } = require("node:child_process");
const [mediaDir,workDir,outputDir,ffmpeg] = process.argv.slice(2);

function run(args) {
  const result=spawnSync(ffmpeg,["-hide_banner","-nostdin",...args],{stdio:"inherit"});
  if(result.error)throw result.error;
  if(result.status!==0)throw new Error("FFmpeg exited with status "+result.status);
}
function locateBrowser(){
  return [
    process.env.CHROME_PATH,
    path.join(process.env.PROGRAMFILES||"","Google/Chrome/Application/chrome.exe"),
    path.join(process.env["PROGRAMFILES(X86)"]||"","Microsoft/Edge/Application/msedge.exe")
  ].find(p=>p&&fs.existsSync(p));
}
async function prepare(page,level){
  const url=pathToFileURL(path.join(__dirname,"speed-trials.html"));
  url.searchParams.set("level",level);url.searchParams.set("plates","1");
  await page.goto(url.href);
  await page.evaluate(()=>window.speedTrial.ready);
  const config=await page.evaluate(()=>window.speedTrial.config);
  const keys=await page.evaluate(()=>Array.from(
    {length:window.speedTrial.config.frames},(_,i)=>window.speedTrial.key(i)));
  const plateDir=path.join(workDir,"header-"+level);
  fs.mkdirSync(plateDir,{recursive:true});
  const png=i=>path.join(plateDir,"frame-"+String(i).padStart(4,"0")+".png");
  let lastKey,master;
  for(let i=0;i<keys.length;i++){
    const filename=png(i);
    if(keys[i]!==lastKey){
      const pixels=await page.evaluate(f=>window.speedTrial.render(f),i);
      // Existing frames may be hard linked after an earlier export.
      if(fs.existsSync(filename))fs.unlinkSync(filename);
      fs.writeFileSync(filename,Buffer.from(pixels,"base64"));
      lastKey=keys[i];master=filename;
    }else{
      if(fs.existsSync(filename))fs.unlinkSync(filename);
      fs.linkSync(master,filename);
    }
  }
  return {level,config,plateDir};
}
function encode({level,config,plateDir}){
  const input=[];
  for(const a of config.angles){
    // Round down to the microsecond so an integral source frame is retained.
    const seconds=(Math.floor((a.start-config.lead)*1e6/60)/1e6).toFixed(6);
    input.push("-threads","3","-ss",seconds,"-i",path.join(mediaDir,a.source));
  }
  input.push("-framerate","60","-i",path.join(plateDir,"frame-%04d.png"));
  // Each side retains the source's entire height. Only the far edges of the
  // ultrawide frame are cropped. The bottom edge stays visible at both gates.
  const panel="fps=60,crop=trunc(ih*1290/906/2)*2:ih,"+
    "scale=1290:906:flags=lanczos,format=yuv420p,setsar=1,settb=1/60,setpts=N";
  const withSpeed=(index,name)=>[
    "["+index+":v]split["+name+"Scene]["+name+"Hud]",
    "["+name+"Scene]"+panel+"["+name+"Base]",
    "["+name+"Hud]fps=60,crop=126:42:45:1368,scale=252:84:flags=lanczos,"+
      "pad=260:92:4:4:color=0x171511,setsar=1,settb=1/60,setpts=N["+name+"Readout]",
    "["+name+"Base]["+name+"Readout]overlay=x=36:y=526:enable='gte(n,"+config.lead+")'["+name+"]"
  ];
  const filter=[
    ...withSpeed(0,"left"),...withSpeed(1,"right"),
    "[left][right]hstack=inputs=2,drawbox=x=1288:y=0:w=4:h=ih:c=0x101010:t=fill[pair]",
    "[2:v]scale=2580:174:out_color_matrix=bt709:in_range=full:out_range=limited,"+
      "format=yuv420p,setsar=1,settb=1/60,setpts=N[header]",
    "[header][pair]vstack=inputs=2:shortest=1,trim=end_frame="+config.frames+"[v]",
    "[0:a:0]aresample=48000:async=1:first_pts=0,aformat=channel_layouts=stereo,"+
      "apad,atrim=end_sample="+config.frames*800+",afade=t=in:st=0:d=0.04,"+
      "afade=t=out:st="+(config.frames/60-0.05)+":d=0.05,asetpts=N/SR/TB[a]"
  ].join(";");
  const filename="speed-v2-level"+level+".mp4";
  const temp=path.join(workDir,filename);
  run(["-loglevel","error","-y",...input,"-filter_complex_threads","3","-filter_complex",filter,
    "-map","[v]","-map","[a]","-t",String(config.frames/60),
    "-r","60","-c:v","h264_nvenc","-preset","p5","-tune","hq","-rc","vbr","-b:v","18M","-maxrate","35M",
    "-bufsize","40M","-spatial-aq","1","-bf","1","-g","120","-profile:v","main","-level:v","5.1",
    "-pix_fmt","yuv420p","-colorspace","bt709","-color_primaries","bt709","-color_trc","bt709","-color_range","tv",
    "-c:a","aac","-ac","2","-ar","48000","-b:a","192k","-movflags","+faststart",temp]);
  const target=path.join(outputDir,filename);
  fs.renameSync(temp,target);
  console.log("Rendered "+target);
}
async function main(){
  if(!ffmpeg)throw new Error("Supply media, work, output, and FFmpeg paths.");
  fs.mkdirSync(outputDir,{recursive:true});
  const modulePaths=[__dirname,path.resolve(path.dirname(process.execPath),"..")];
  const {chromium}=require(require.resolve("playwright",{paths:modulePaths}));
  const browser=await chromium.launch({headless:true,executablePath:locateBrowser()});
  let prepared;
  try{
    const page=await browser.newPage();
    prepared=[];
    for(const level of ["2","10"])prepared.push(await prepare(page,level));
  }finally{await browser.close();}
  for(const plan of prepared)encode(plan);
}
main().catch(e=>{console.error(e);process.exitCode=1;});

