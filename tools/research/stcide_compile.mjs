import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
const root=path.dirname(fileURLToPath(import.meta.url));
const request=JSON.parse(fs.readFileSync(process.argv[2],'utf8').replace(/^\uFEFF/,''));
const output=process.argv[3];
globalThis.self={postMessage(message){
  fs.writeFileSync(output,JSON.stringify(message,(k,v)=>v instanceof Map?Object.fromEntries(v):v instanceof Set?[...v]:ArrayBuffer.isView(v)?Array.from(v):v,2));
  console.log(JSON.stringify({ok:message.ok,success:message.result?.success,buildVer:message.buildVer,fullMs:message.fullMs,error:message.error||message.result?.error,keys:Object.keys(message.result||{}),memory:message.result?.memReport}));
}};
// The vendor worker already has a local fallback when nested browser Workers are unavailable.
// No compiler source or compiler options are patched by this adapter.
await import(pathToFileURL(path.join(process.argv[4] || path.join(root,'vendor'),'ide.worker.js')));
self.onmessage({data:{id:1,mode:'full',ver:1,target:'AI8051U',opts:{eepromSize:1024},...request}});
