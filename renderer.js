/*{
    const strIn = document.getElementById("strIn");
    const strOut = document.getElementById("strOut");
    const strSend = document.getElementById("strSend");

    strSend.onclick = function(ev){
        console.log(window.advancedfx);
        window.advancedfx.jsonRequest(strIn.value).then((result)=>{
            strOut.value = result;
        });
    }
}*/
{
    let resultBox = document.getElementById("strOut");
    let evType = "";
    function log(e) {
        let oldContent = resultBox.textContent;
        let addContent = "";
        for(const key in e) {
            addContent = addContent + key.toString()+": "+(e[key]?e[key].toString():e[key])+"\n";
        }
        resultBox.textContent = "==== ==== ("+evType+") ==== ====\n" + addContent + oldContent.substring(0,Math.min(4096,oldContent.length))
    }
    document.addEventListener("mousedown", (e)=>{evType=(e.target && (e.target.tagName == "BODY" || e.target.tagName=="HTML")) ? "mousedown-YES" : "mousedown-NO";log(e)});
    document.getElementById("strIn").addEventListener("keydown", (e)=>{evType="keydown";log(e)});
    document.getElementById("strIn").addEventListener("keypress", (e)=>{evType="keypress";log(e)});
    document.getElementById("strIn").addEventListener("keyup", (e)=>{evType="keyup";log(e)});
}