function S = getStationaryPeriods(D, varargin)
% 
%   S = getStationaryPeriods(D, varargin)
%
%   Estimate times in which the sensor is not moving. This function
%   estimates the standard deviation per accelerometer axis in epochs. It
%   returns those samples (mean over epoch) for times the sensor is not
%   moving, to be used in automated calibraction procedure (estimateCalibration.m). 
%
%   Input: 
%       D   [struct]        As read from AX3_readFile.m, contains at least fields:
%                           .ACC    [Nx4]   time [matlab], X, Y, Z [g]
%                           .TEMP   [Mx2]   time [matlab], temp [C]
%
%   Optional arguments ('name',value):
%       actThresh   double  Threshold for activity (default: 0.01 [g])
%       wlen        double  Duration of epoch (detault: 1 [s])
%       wstep       double  Time between epochs (default: 1 [s])
%       tBounds     double  +- boundaries for calculation of mean
%                           temperature (default: 3 [s]);
%       progress    0/1     Show progress bar (default: 0=no)
%       startTime   mtime   Start time (in matlab time) to look for
%                           stationary episodes (default: -1 = start of file)
%       stopTime    mtime   Stopping time (default: -1 = end of file)
%
%   Output:
%       S   [Nx4]       Samples time,XYZ,temp from stationary periods (mean over
%                       epoch).
%
%
%   Nils Hammerla, '14
%   <nils.hammerla@ncl.ac.uk>
%

% 
% Copyright (c) 2014, Newcastle University, UK.
% All rights reserved.
% 
% Redistribution and use in source and binary forms, with or without 
% modification, are permitted provided that the following conditions are met: 
% 1. Redistributions of source code must retain the above copyright notice, 
%    this list of conditions and the following disclaimer.
% 2. Redistributions in binary form must reproduce the above copyright notice, 
%    this list of conditions and the following disclaimer in the documentation 
%    and/or other materials provided with the distribution.
% 
% THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
% AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
% IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
% ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
% LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
% CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
% SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
% INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
% CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
% ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
% POSSIBILITY OF SUCH DAMAGE. 
% 


p = inputParser;
    
% define optional arguments with default values
addOptional(p,'actThresh', 0.01, @isnumeric); % activity thresholds on std    
addOptional(p,'wlen',         1, @isnumeric); % epoch duration
addOptional(p,'wstep',        1, @isnumeric); % step between epochs      
addOptional(p,'tBounds',      3, @isnumeric); % bounds for mean temp.       
addOptional(p,'progress',     0, @isnumeric); % show progress bar
addOptional(p,'startTime',    0, @isnumeric); % start time (matlab)           
addOptional(p,'stopTime',     0, @isnumeric); % stop time (matlab)           

% parse varargin
parse(p,varargin{:});
p = p.Results;

% parse again 
actThresh =  p.actThresh;  
wlen = p.wlen/86400;     
wstep = p.wstep/86400;    
tBounds = p.tBounds/86400; 
progress = p.progress;     

% initialise return variable
S = [];

% display progress bar
if progress,
    h = waitbar(0,'Estimating stationary periods');
    cnt = 1;
end

% go through data and get periods of stationarity
st = D.ACC(1,1);         
en = D.ACC(end,1)-wlen;  

if p.startTime > 0,
    st = p.startTime;
end
if p.stopTime >0 && p.stopTime > st,
    en = p.stopTime;
end
    
for t=st:wstep:en,
    
    % samples in epoch
    ind = D.ACC(:,1)>= t & D.ACC(:,1) <= t+wlen;
    
    % if there are samples in the epoch
    if sum(ind)>10,
        % get standard deviation per axis
        sd = std(D.ACC(ind,2:4),0,1);
        
        % check if std is below threshold for each axis
        if sum(sd <= actThresh)>=3,
            % stationary!
            % get temperate (use wider bounds as lower sampling frequency)
            indT = D.TEMP(:,1)>= t-tBounds & D.TEMP(:,1) <= t+wlen+tBounds;
            % save mean of epoch measurements and mean temperature
            S = [S ; t+wlen/2 mean(D.ACC(ind,2:4)) mean(D.TEMP(indT,2))];
        end
    end
    
    % update progress bar if necessary
    if progress
        if mod(cnt,100) == 0,
            waitbar((t-st) / (en-st),h)
        end
        cnt = cnt + 1;
    end
end

% close progress bar
close(h);

end