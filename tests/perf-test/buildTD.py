import os
import subprocess

class BuildTDengine:
    def __init__(self, host='vm96', path = '/root/pxiao/TDengine', branch = 'main', dataDir = '/var/lib/taos') -> None:
        self.host = host
        self.path = path
        self.branch = branch
        self.dataDir = dataDir
    
    def build(self):
        parameters=[self.path, self.branch]
        build_fild = "./build.sh"
        try:
            # Run the Bash script using subprocess
            subprocess.run(['bash', build_fild] + parameters, check=True)
            print("TDengine build successfully.")
        except subprocess.CalledProcessError as e:
            print(f"Error running Bash script: {e}")
        except FileNotFoundError as e:
            print(f"File not found: {e}")
    
    def get_cmd_output(self, cmd):        
        try:
            # Run the Bash command and capture the output
            result = subprocess.run(cmd, stdout=subprocess.PIPE, shell=True, text=True)
            
            # Access the output from the 'result' object
            output = result.stdout
            
            return output.strip()
        except subprocess.CalledProcessError as e:
            print(f"Error running Bash command: {e}")
    
    def create_taos_cfg_file(self) -> str:
        
        taos_cfg_file = "/tmp/taos.cfg"

        os.system(f"echo 'firstEp {self.host}:6030' > {taos_cfg_file}")
        os.system(f"echo 'dataDir {self.dataDir}' >> {taos_cfg_file}")
        os.system(f"echo 'debugFlag 131' >> {taos_cfg_file}")

        return taos_cfg_file
    
    def start_taosd(self):
        os.system("systemctl stop taosd")
        os.system("pkill -9 taosd")        
        os.system("nohup taosd -c /tmp > /dev/null 2>&1 &")