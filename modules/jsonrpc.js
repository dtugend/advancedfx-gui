class JsonRpc_2_0_Server {

    constructor(asyncFnReadString, asyncFnWriteString) {
        this.active = true;
        this.fns = {};
        this.fnRead = asyncFnReadString;
        this.fnWrite = asyncFnWriteString;
    }

    on(methodName, asyncFn) {
        this.fns[methodName] = asyncFn;
    }

    async pump() {
        let self = this;

        async function handleRequest(request) {
            //console.log(request);
            if(request["jsonrpc"] !== "2.0") throw "Not a JSON-RPC 2.0 request";
            let result = await self.fns[request["method"]].apply(null, request["params"]);
            if(result !== undefined) return {
                "jsonrpc": "2.0",
                "result": result,
                "id": request["id"]
            };
            return undefined;
        }

        async function handleRequests(requests) {
            let results = requests.array.forEach(request => {
                return handleRequest(request);
            });

            results = Promise.all(results).filter(value => {
                return value !== undefined;
            });

            return 0 < results.length ? results : undefined;
        }

        while(this.active) {
            let request = JSON.parse(await this.fnRead());

            let result = Array.isArray(request) ? await handleRequests(request) : await handleRequest(request);

            await this.fnWrite(result !== undefined ? JSON.stringify(result) : "");
        }
    }

    quit() {
        this.active = false;
    }
}

module.exports = { JsonRpc_2_0_Server }