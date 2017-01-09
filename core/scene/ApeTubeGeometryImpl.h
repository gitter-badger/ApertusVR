/*MIT License

Copyright (c) 2016 MTA SZTAKI

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/

#ifndef APE_TUBEGEOMETRYIMPL_H
#define APE_TUBEGEOMETRYIMPL_H

#include "ApeITubeGeometry.h"
#include "ApeEventManagerImpl.h"
#include "ApeIScene.h"
#include "ApeReplica.h"

namespace Ape
{
	class TubeGeometryImpl : public Ape::ITubeGeometry, public Ape::Replica
	{
	public:
		TubeGeometryImpl(std::string name, bool isHostCreated);

		~TubeGeometryImpl();

		void setParameters(float height, float tile);

		Ape::GeometryTubeParameters getParameters();

		void setParentNode(Ape::NodeWeakPtr parentNode);

		void setMaterial(Ape::MaterialWeakPtr material);

		Ape::MaterialWeakPtr getMaterial();

		void WriteAllocationID(RakNet::Connection_RM3 *destinationConnection, RakNet::BitStream *allocationIdBitstream) const override;

		RakNet::RM3SerializationResult Serialize(RakNet::SerializeParameters *serializeParameters) override;

		void Deserialize(RakNet::DeserializeParameters *deserializeParameters) override;

	private:
		Ape::EventManagerImpl* mpEventManagerImpl;

		Ape::IScene* mpScene;

		Ape::GeometryTubeParameters mParameters;

		Ape::MaterialWeakPtr mMaterial;

		std::string mMaterialName;
	};
}

#endif
